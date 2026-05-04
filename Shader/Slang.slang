#include "Shared.h"

ConstantBuffer<Constants> mConstants : REGISTER_CBV(ROOT_CBV_REGISTER, COMMON_ROOT_SIGNATURE_REGISTER_SPACE);
static RWTexture2D<float4>.Handle ScreenDebugUAV = RWTexture2D<float4>.Handle(uint2((uint)ViewDescriptorIndex::ScreenDebugUAV, 0));

[Differentiable]
float sqr_autodiff(float x)
{
    return x * x;
}

[shader("compute")]
[numthreads(8,8,1)]
void SlangCS(uint3 inDispatchThreadID : SV_DispatchThreadID)
{
    float2 xy_uniform = (inDispatchThreadID.xy + 0.5) / float2(mConstants.mScreenWidth, mConstants.mScreenHeight);
    float x = xy_uniform.x;
    float y = xy_uniform.y;

    let x_pair = diffPair(x, 1.0f /* = dx/dx for initialization */);
    let x_sqr_pair = fwd_diff(sqr_autodiff)(x_pair);

    float border = 0.01;
    // float border = 0.0;

    float value = 0;
    if (y >= 0.00 + border && y < 0.25 - border) value = x * x;
    if (y >= 0.25 + border && y < 0.50 - border) value = x_sqr_pair.p;
    if (y >= 0.50 + border && y < 0.75 - border) value = 2 * x;
    if (y >= 0.75 + border && y < 1.00 - border) value = x_sqr_pair.d;

    ScreenDebugUAV[inDispatchThreadID.xy] = float4(value, 0, 0, 1);
}
