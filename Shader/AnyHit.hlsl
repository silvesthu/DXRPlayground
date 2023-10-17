
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("anyhit")]
void AnyHit(inout RayPayload ioPayload, in BuiltInTriangleIntersectionAttributes inAttributes)
{
	if (inAttributes.barycentrics.x < 0.1)
		IgnoreHit();
}
