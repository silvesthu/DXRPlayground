
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("closesthit")]
void ClosestHit100(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	payload.mData = float4(1, 0, 0, 0);
}