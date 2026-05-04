#include "Shared.h"

[shader("compute")]
[numthreads(8,8,1)]
void HelloWorldSlangCS(uint3 inDispatchThreadID : SV_DispatchThreadID)
{
    RWTexture2D<float4>.Handle ScreenDebugUAV = RWTexture2D<float4>.Handle(uint2((uint)ViewDescriptorIndex::ScreenDebugUAV, 0));
}
