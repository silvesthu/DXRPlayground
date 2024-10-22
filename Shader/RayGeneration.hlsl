
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

[shader("raygeneration")]
void RayGeneration()
{
	float2 screen_coords						= float2(DispatchRaysIndex().xy) + 0.5;
	float2 screen_size							= float2(DispatchRaysDimensions().xy);

	float2 ndc_xy								= ((screen_coords / screen_size) * 2.f - 1.f);							// [0,1] => [-1,1]
	ndc_xy.y									= -ndc_xy.y;															// Flip y
	float4 point_on_near_plane					= mul(mConstants.mInverseProjectionMatrix, float4(ndc_xy, 0.0, 1.0));
	float3 ray_direction_vs						= normalize(point_on_near_plane.xyz / point_on_near_plane.w);
	float3 ray_direction_ws						= mul(mConstants.mInverseViewMatrix, float4(ray_direction_vs, 0.0)).xyz;

	RayDesc ray;
	ray.Origin									= mConstants.CameraPosition().xyz;
	ray.Direction								= ray_direction_ws;
	ray.TMin									= 0.001;
	ray.TMax									= 100000;

	RayPayload payload							= (RayPayload)0;
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

	ScreenColorUAV[DispatchRaysIndex().xy]	= payload.mData;
}