#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[RootSignature(ROOT_SIGNATURE_COMMON)]
[shader("compute")]
[numthreads(64,1,1)]
void HelloWorldCS(uint3 inDispatchThreadID : SV_DispatchThreadID)
{
}
