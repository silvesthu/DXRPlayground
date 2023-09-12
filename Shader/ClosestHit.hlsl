
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	payload.mData = float4(1, 0, 1, 0);
}