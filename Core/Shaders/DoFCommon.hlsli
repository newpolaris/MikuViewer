cbuffer CB0 : register(b0)
{
    float FocusCenter;
    float FocalRange;
    float FocalMinDist; // Closer than this is max blurriness
    float FocalMaxDist; // Farther than this is also max blurriness
    float2 RcpBufferDim;
}

#define DEPTH_FOREGROUND_RANGE 0.01
#define MATH_CONST_PI 3.1415926535897
#define MAX_COC_RADIUS 16.0
#define RING1_THRESHOLD 1.0
#define RING2_THRESHOLD 6.0
#define RING3_THRESHOLD 11.0

float Max3( float a, float b, float c) { return max(max(a, b), c); }
float Min3( float a, float b, float c) { return min(min(a, b), c); }
float Med3( float a, float b, float c) { return clamp(a, min(b, c), max(b, c)); }
float Max4( float a, float b, float c, float d) { return Max3(a, b, max(c, d)); }
float Min4( float a, float b, float c, float d) { return Min3(a, b, min(c, d)); }
float Max4( float4 vec ) { return Max4( vec.x, vec.y, vec.z, vec.w ); }
float Min4( float4 vec ) { return Min4( vec.x, vec.y, vec.z, vec.w ); }

float ComputeCoC(float Depth)
{
     return saturate( abs(Depth - FocusCenter)  / FocalRange );
}