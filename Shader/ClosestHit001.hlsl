
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "ClosestHit.h"

[shader("closesthit")]
void ClosestHit001(inout RayPayload ioPayload, in BuiltInTriangleIntersectionAttributes inAttributes)
{
	ioPayload.mData = float4(0, 0, 1, 0);

	ClosestHitOverride(ioPayload, inAttributes);
}

[shader("anyhit")]
void AnyHit001(inout RayPayload ioPayload, in BuiltInTriangleIntersectionAttributes inAttributes)
{
	if (inAttributes.barycentrics.y > 0.5)
		IgnoreHit();
}
