#include "../Common.h"

//
// Synopsis: Example program illustrating how to use the Seb library
//
// Authors: Martin Kutz <kutz@math.fu-berlin.de>,
//          Kaspar Fischer <kf@iaeth.ch>

#include <iostream>
#include <cstdio>

#include "Seb.h"

TEST(MiniBallTest, Example)
{
    typedef double FT;
    typedef Seb::Point<FT> Point;
    typedef std::vector<Point> PointVector;
    typedef Seb::Smallest_enclosing_ball<FT> Miniball;

    using std::cout;
    using std::endl;
    using std::vector;

    const int n = 100, d = 100;
    const bool on_boundary = false;

    // Construct n random points in dimension d
    PointVector S;
    vector<double> coords( d );
    srand( clock() );
    for (int i = 0; i < n; ++i) {

        // Generate coordindates in [-1,1]
        double len = 0;
        for (int j = 0; j < d; ++j) {
            coords[j] = static_cast<FT>(2.0*rand() / RAND_MAX - 1.0);
            len += coords[j] * coords[j];
        }

        // Normalize length to "almost" 1 (makes it harder for the algorithm)
        if (on_boundary) {
            const double Wiggle = 1e-2;
            len = 1 / (std::sqrt( len ) + Wiggle*rand() / RAND_MAX);
            for (int j = 0; j < d; ++j)
                coords[j] *= len;
        }
        S.push_back( Point( d, coords.begin() ) );
    }
    cout << "Starting computation..." << endl
        << "====================================================" << endl;
    // Seb::Timer::instance().start( "all" );

    // Compute the miniball by inserting each value
    Miniball mb( d, S );

    // Output
    FT rad = mb.radius();
    FT rad_squared = mb.squared_radius();
    cout // << "Running time: " << Seb::Timer::instance().lapse( "all" ) << "s" << endl
        << "Radius = " << rad << " (squared: " << rad_squared << ")" << endl
        << "Center:" << endl;

    Miniball::Coordinate_iterator center_it = mb.center_begin();
    for (int j = 0; j < d; ++j)
        cout << "  " << center_it[j] << endl;
    cout << "=====================================================" << endl;

    mb.verify();
    cout << "=====================================================" << endl;
}
