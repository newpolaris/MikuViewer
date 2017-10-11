#include <memory>
#include <vector>

#include "GameCore.h"
#include "EngineTuning.h"
#include "Utility.h"
#include "VectorMath.h"
#define BT_THREADSAFE 1
#define BT_NO_SIMD_OPERATOR_OVERLOADS 1
#include "Physics.h"
#include "btBulletDynamicsCommon.h"
#include "BaseRigidBody.h"
#include "BulletDebugDraw.h"
#include "MultiThread.inl"
#include "LinearMath/btThreads.h"
#include "LinearMath/btQuickprof.h"
#include "TextUtility.h"
#include "BulletSoftBody/btSoftBodyHelpers.h"
#include "BulletSoftBody/btSoftBodyRigidBodyCollisionConfiguration.h"
#include "BulletSoftBody/btSoftRigidDynamicsWorld.h"
#include "BulletDynamics/Dynamics/InplaceSolverIslandCallbackMt.h"

namespace Physics
{
    // Use interpolation to set body transfrom, it could case some gap betweeen
    // debug polygon and object position
    BoolVar s_bInterpolation( "Application/Physics/Motion Interpolation", true );
    BoolVar s_bDebugDraw( "Application/Physics/Debug Draw", false );

    // bullet needs to define BT_THREADSAFE and (BT_USE_OPENMP || BT_USE_PPL || BT_USE_TBB)
    const bool bMultithreadCapable = true;
    const float EarthGravity = 9.8f;
    SolverType m_SolverType = SOLVER_TYPE_SEQUENTIAL_IMPULSE;
    int m_SolverMode = SOLVER_SIMD |
        SOLVER_USE_WARMSTARTING |
        // SOLVER_RANDMIZE_ORDER |
        // SOLVER_INTERLEAVE_CONTACT_AND_FRICTION_CONSTRAINTS |
        // SOLVER_USE_2_FRICTION_DIRECTIONS |
        0;

	btSoftRigidDynamicsWorld* g_DynamicsWorld = nullptr;

    std::unique_ptr<btDefaultCollisionConfiguration> Config;
    std::unique_ptr<btBroadphaseInterface> Broadphase;
    std::unique_ptr<btCollisionDispatcher> Dispatcher;
    std::unique_ptr<btConstraintSolver> Solver;
    std::unique_ptr<btSoftRigidDynamicsWorld> DynamicsWorld;
    std::unique_ptr<BulletDebug::DebugDraw> DebugDrawer;
    btSoftBodyWorldInfo SoftBodyWorldInfo;
    btSoftBodyWorldInfo* g_SoftBodyWorldInfo = &SoftBodyWorldInfo;
    btConstraintSolver* CreateSolverByType( SolverType t );
};

using namespace Physics;

void EnterProfileZoneDefault(const char* name)
{
    PushProfilingMarker( Utility::MakeWStr(std::string(name)), nullptr );
}

void LeaveProfileZoneDefault()
{
    PopProfilingMarker( nullptr );
}

btConstraintSolver* Physics::CreateSolverByType( SolverType t )
{
    btMLCPSolverInterface* mlcpSolver = NULL;
    switch (t)
    {
    case SOLVER_TYPE_SEQUENTIAL_IMPULSE:
        return new btSequentialImpulseConstraintSolver();
    case SOLVER_TYPE_NNCG:
        return new btNNCGConstraintSolver();
    case SOLVER_TYPE_MLCP_PGS:
        mlcpSolver = new btSolveProjectedGaussSeidel();
        break;
    case SOLVER_TYPE_MLCP_DANTZIG:
        mlcpSolver = new btDantzigSolver();
        break;
    case SOLVER_TYPE_MLCP_LEMKE:
        mlcpSolver = new btLemkeSolver();
        break;
    default: {}
    }
    if (mlcpSolver)
        return new btMLCPSolver( mlcpSolver );
    return NULL;
}

void Physics::Initialize( void )
{
    BulletDebug::Initialize();
    gTaskMgr.init(4);

    btSetCustomEnterProfileZoneFunc(EnterProfileZoneDefault);
    btSetCustomLeaveProfileZoneFunc(LeaveProfileZoneDefault);

    if (bMultithreadCapable)
    {
        btDefaultCollisionConstructionInfo cci;
        cci.m_defaultMaxPersistentManifoldPoolSize = 80000;
        cci.m_defaultMaxCollisionAlgorithmPoolSize = 80000;
        Config = std::make_unique<btSoftBodyRigidBodyCollisionConfiguration >( cci );

#if USE_PARALLEL_NARROWPHASE
        Dispatcher = std::make_unique<MyCollisionDispatcher>( Config.get() );
#else
        Dispatcher = std::make_unique<btCollisionDispatcher>( Config.get() );
#endif //USE_PARALLEL_NARROWPHASE

        Broadphase = std::make_unique<btDbvtBroadphase>();

#if USE_PARALLEL_ISLAND_SOLVER
        {
            btConstraintSolver* solvers[ BT_MAX_THREAD_COUNT ];
            int maxThreadCount = btMin( int(BT_MAX_THREAD_COUNT), TaskManager::getMaxNumThreads() );
            for ( int i = 0; i < maxThreadCount; ++i )
                solvers[ i ] = CreateSolverByType( m_SolverType );
            Solver.reset( new MyConstraintSolverPool( solvers, maxThreadCount ) );
        }
#else
        Solver.reset( CreateSolverByType( m_SolverType ) );
#endif //#if USE_PARALLEL_ISLAND_SOLVER
        DynamicsWorld = std::make_unique<btSoftRigidDynamicsWorld>( Dispatcher.get(), Broadphase.get(), Solver.get(), Config.get() );

#if USE_PARALLEL_ISLAND_SOLVER
        if ( btSimulationIslandManagerMt* islandMgr = dynamic_cast<btSimulationIslandManagerMt*>( DynamicsWorld->getSimulationIslandManager() ) )
            islandMgr->setIslandDispatchFunction( parallelIslandDispatch );
#endif //#if USE_PARALLEL_ISLAND_SOLVER
    }
    else
    {
        Config = std::make_unique<btSoftBodyRigidBodyCollisionConfiguration>();
        Broadphase = std::make_unique<btDbvtBroadphase>();
        Dispatcher = std::make_unique<btCollisionDispatcher>( Config.get() );
        Solver = std::make_unique<btSequentialImpulseConstraintSolver>();
        Solver.reset( CreateSolverByType( m_SolverType ) );
        DynamicsWorld = std::make_unique<btSoftRigidDynamicsWorld>( Dispatcher.get(), Broadphase.get(), Solver.get(), Config.get() );
    }
    SoftBodyWorldInfo.m_broadphase = Broadphase.get();
    SoftBodyWorldInfo.m_dispatcher = Dispatcher.get();
    SoftBodyWorldInfo.m_gravity = DynamicsWorld->getGravity();
    SoftBodyWorldInfo.m_sparsesdf.Initialize();

    ASSERT( DynamicsWorld != nullptr );
    DynamicsWorld->setGravity( btVector3( 0, -EarthGravity, 0 ) );
    DynamicsWorld->setInternalTickCallback( profileBeginCallback, NULL, true );
    DynamicsWorld->setInternalTickCallback( profileEndCallback, NULL, false );
    DynamicsWorld->getSolverInfo().m_solverMode = m_SolverMode;

    DebugDrawer = std::make_unique<BulletDebug::DebugDraw>();
    DebugDrawer->setDebugMode(
        // btIDebugDraw::DBG_DrawAabb |
        // btIDebugDraw::DBG_DrawConstraints |
        // btIDebugDraw::DBG_DrawConstraintLimits |
        btIDebugDraw::DBG_DrawWireframe
    );
    DynamicsWorld->setDebugDrawer( DebugDrawer.get() );

    g_DynamicsWorld = DynamicsWorld.get();
}

void Physics::Shutdown( void )
{
    gTaskMgr.shutdown();
    SoftBodyWorldInfo.m_sparsesdf.Reset();
    BulletDebug::Shutdown();

    int Len = (int)DynamicsWorld->getSoftBodyArray().size();
    for (int i = Len -1; i >= 0; i--)
    {
        btSoftBody*	psb = DynamicsWorld->getSoftBodyArray()[i];
        DynamicsWorld->removeSoftBody( psb );
        delete psb;
    }
    ASSERT(DynamicsWorld->getNumCollisionObjects() == 0,
        "Remove all rigidbody objects from world");

    DynamicsWorld.reset( nullptr );
    g_DynamicsWorld = nullptr;
}

void Physics::Update( float deltaT )
{
    DynamicsWorld->setLatencyMotionStateInterpolation( s_bInterpolation );
    ASSERT(DynamicsWorld.get() != nullptr);
    DynamicsWorld->stepSimulation( deltaT, 1 );
}

void Physics::Render( GraphicsContext& Context, const Math::Matrix4& ClipToWorld )
{
    ASSERT( DynamicsWorld.get() != nullptr );
    if (s_bDebugDraw)
    {
        DynamicsWorld->debugDrawWorld();
        for (int i = 0; i < DynamicsWorld->getSoftBodyArray().size(); i++)
		{
            btSoftBody*	psb = DynamicsWorld->getSoftBodyArray()[i];
            btSoftBodyHelpers::DrawFrame( psb, DynamicsWorld->getDebugDrawer() );
            btSoftBodyHelpers::Draw( psb, DynamicsWorld->getDebugDrawer(), DynamicsWorld->getDrawFlags() );
		}
        DebugDrawer->flush( Context, ClipToWorld );
    }
}

void Physics::Profile( ProfileStatus& Status )
{
    Status.NumIslands = gNumIslands;
    Status.NumCollisionObjects = DynamicsWorld->getNumCollisionObjects();
    int numContacts = 0;
    int numManifolds = Dispatcher->getNumManifolds();
    for (int i = 0; i < numManifolds; ++i)
    {
        const btPersistentManifold* man = Dispatcher->getManifoldByIndexInternal( i );
        numContacts += man->getNumContacts();
    }
    Status.NumManifolds = numManifolds;
    Status.NumContacts = numContacts;
    Status.NumThread = gTaskMgr.getNumThreads();
    Status.InternalTimeStep = gProfiler.getAverageTime( Profiler::kRecordInternalTimeStep )*0.001f;
    if (bMultithreadCapable)
    {
        Status.DispatchAllCollisionPairs = gProfiler.getAverageTime( Profiler::kRecordDispatchAllCollisionPairs )*0.001f;
        Status.DispatchIslands = gProfiler.getAverageTime( Profiler::kRecordDispatchIslands )*0.001f;
        Status.PredictUnconstrainedMotion = gProfiler.getAverageTime( Profiler::kRecordPredictUnconstrainedMotion )*0.001f;
        Status.CreatePredictiveContacts = gProfiler.getAverageTime( Profiler::kRecordCreatePredictiveContacts )*0.001f;
        Status.IntegrateTransforms = gProfiler.getAverageTime( Profiler::kRecordIntegrateTransforms )*0.001f;
    }
}

