cbuffer CB0 : register(b0)
{
    float FocusCenter;
    float FocalRange;
    float FocalMinDist; // Closer than this is max blurriness
    float FocalMaxDist; // Farther than this is also max blurriness
    float2 RcpBufferDim;
}

float ComputeCoC(float Depth)
{
    return saturate(abs(Depth - FocusCenter) / FocalRange);
}