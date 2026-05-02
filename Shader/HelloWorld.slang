#include "Shared.h"

[shader("compute")]
[numthreads(64,1,1)]
void HelloWorldCS(uint3 inDispatchThreadID : SV_DispatchThreadID)
{
}
