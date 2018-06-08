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
    float coc = (Depth - FocusCenter) / FocalRange;
    return clamp(coc, -1, 1);
    // saturate((Depth - FocusCenter) / FocalRange);
}