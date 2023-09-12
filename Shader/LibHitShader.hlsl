
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("closesthit")]
void Hit100(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	payload.mData = float4(1, 0, 0, 0);
}

[shader("closesthit")]
void Hit010(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	payload.mData = float4(0, 1, 0, 0);
}

[shader("closesthit")]
void Hit001(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	payload.mData = float4(0, 0, 1, 0);
}
