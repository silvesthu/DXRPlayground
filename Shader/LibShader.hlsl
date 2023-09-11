
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("miss")]
void Miss(inout RayPayload payload)
{
	payload.mData = 0.1;
}

[shader("closesthit")]
void Hit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	payload.mData = float4(1, 0, 1, 0);
}

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

[shader("raygeneration")]
void RayGeneration()
{
	uint3 launchIndex = sGetDispatchRaysIndex();
	uint3 launchDim = sGetDispatchRaysDimensions();

	float2 crd = float2(launchIndex.xy) + 0.5;
	float2 dims = float2(launchDim.xy);

	float2 d = ((crd / dims) * 2.f - 1.f); // 0~1 => -1~1
	d.y = -d.y;

	RayDesc ray;
	ray.Origin = mConstants.mCameraPosition.xyz;
	ray.Direction = normalize(mConstants.mCameraDirection.xyz + mConstants.mCameraRightExtend.xyz * d.x + mConstants.mCameraUpExtend.xyz * d.y);
	ray.TMin = 0.001;
	ray.TMax = 100000;

	RayPayload payload = (RayPayload)0;
	payload.mData = 0;

	TraceRay(
		RaytracingScene, 		// RaytracingAccelerationStructure
		0,						// RayFlags 
		0xFF,					// InstanceInclusionMask
		0,						// RayContributionToHitGroupIndex, 4bits
		0,						// MultiplierForGeometryContributionToHitGroupIndex, 16bits
		0,						// MissShaderIndex
		ray,					// RayDesc
		payload					// Payload
	);

	ScreenColorUAV[sGetDispatchRaysIndex().xy] = payload.mData;
}