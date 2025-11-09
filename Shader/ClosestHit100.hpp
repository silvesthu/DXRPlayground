
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "ClosestHit.h"

[shader("closesthit")]
void ClosestHit100(inout RayPayload ioPlayload, in BuiltInTriangleIntersectionAttributes inAttributes)
{
	ioPlayload.mData = float4(1, 0, 0, 0);

	ClosestHitOverride(ioPlayload, inAttributes);
}