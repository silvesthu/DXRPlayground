
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("anyhit")]
void AnyHit(inout RayPayload ioPayload, in BuiltInTriangleIntersectionAttributes inAttributes)
{
	float3 barycentrics = float3(1.0 - inAttributes.barycentrics.x - inAttributes.barycentrics.y, inAttributes.barycentrics.xy);
	if (all(barycentrics > 0.01))
		IgnoreHit();
}
