
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "ClosestHit.h"

[shader("closesthit")]
void ClosestHit010(inout RayPayload ioPayload, in BuiltInTriangleIntersectionAttributes inAttributes)
{
	ioPayload.mData = float4(0, 1, 0, 0);

	ClosestHitOverride(ioPayload, inAttributes);
}