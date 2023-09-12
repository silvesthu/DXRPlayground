
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("closesthit")]
void ClosestHit010(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	payload.mData = float4(0, 1, 0, 0);
}