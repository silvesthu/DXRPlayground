RWTexture2D<float4> RaytracingOutput : register(u0);
RaytracingAccelerationStructure RaytracingScene : register(t0);
cbuffer PerFrame : register(b0)
{
	float4	mBackgroundColor;
	float4	mCameraPosition;
	float4	mCameraDirection;
	float4	mCameraRightExtend;
	float4	mCameraUpExtend;

	uint	mDebugMode;
	uint	mShadowMode;
}

struct InstanceData
{
	float3 mAlbedo;
	float3 mReflectance;
	float3 mEmission;
	float  mRoughness;
};
StructuredBuffer<InstanceData> InstanceDataBuffer : register(t1);

float3 linearToSrgb(float3 c)
{
	// Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	float3 sq1 = sqrt(c);
	float3 sq2 = sqrt(sq1);
	float3 sq3 = sqrt(sq2);
	float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
	return srgb;
}

struct RayPayload
{
	float3 color;
};

struct ShadowPayload
{
	bool hit;
};

[shader("raygeneration")]
void defaultRayGeneration()
{
	uint3 launchIndex = DispatchRaysIndex();
	uint3 launchDim = DispatchRaysDimensions();

	float2 crd = float2(launchIndex.xy);
	float2 dims = float2(launchDim.xy);

	float2 d = ((crd/dims) * 2.f - 1.f); // 0~1 => -1~1
	d.y = -d.y;
	
	RayDesc ray;
	ray.Origin = mCameraPosition;
	ray.Direction = normalize(mCameraDirection + mCameraRightExtend * d.x + mCameraUpExtend * d.y);
	ray.TMin = 0;				// Near
	ray.TMax = 100000;			// Far

	RayPayload payload;
	TraceRay(
		RaytracingScene, 		// RaytracingAccelerationStructure
		0,						// RayFlags 
		0xFF,					// InstanceInclusionMask
		0,						// RayContributionToHitGroupIndex, 4bits
		0,						// MultiplierForGeometryContributuionToHitGroupIndex, 16bits
		0,						// MissShaderIndex
		ray,					// RayDesc
		payload					// payload_t
	);
	float3 col = linearToSrgb(payload.color);
	RaytracingOutput[launchIndex.xy] = float4(col, 1);
}

[shader("miss")]
void defaultMiss(inout RayPayload payload)
{
	payload.color = mBackgroundColor;
}

[shader("closesthit")]
void defaultClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	if (mDebugMode == 1) // Barycentrics
	{
		float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
		payload.color = barycentrics;

		return;
	}

	// See https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html for more system value intrinsics

	// Find the world-space hit position
	float3 hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

	float shadow_factor = 1.0;
	if (mShadowMode == 1) // Test
	{
		// Fire a shadow ray. The direction is hard-coded here, but can be fetched from a constant-buffer
		RayDesc ray;
		ray.Origin = hit_position;
		ray.Direction = normalize(float3(-1, 1, -1));
		ray.TMin = 0.01;
		ray.TMax = 100000;

		// http://intro-to-dxr.cwyman.org/presentations/IntroDXR_RaytracingAPI.pdf
		// HitGroupRecordAddress =
		// 	start + stride * (rayContribution + (geometryMultiplier * geometryContribution) + instanceContribution)
		// where:
		// 	start = D3D12_DISPATCH_RAYS_DESC.HitGroupTable.StartAddress
		// 	stride = D3D12_DISPATCH_RAYS_DESC.HitGroupTable.StrideInBytes
		// 	rayContribution = RayContributionToHitGroupIndex (TraceRay parameter)
		// 	geometryMultiplier = MultiplierForGeometryContributionToHitGroupIndex (TraceRay parameter)
		// 	geometryContribution = index of geometry in bottom-level acceleration structure (0,1,2,3..)
		// 	instanceContribution = D3D12_RAYTRACING_INSTANCE_DESC.InstanceContributionToHitGroupIndex

		ShadowPayload shadowPayload;
		TraceRay(
			RaytracingScene,	// RaytracingAccelerationStructure
			0,					// RayFlags 
			0xFF,				// InstanceInclusionMask
			1,					// RayContributionToHitGroupIndex, 4bits
			0,					// MultiplierForGeometryContributuionToHitGroupIndex, 16bits
			1,					// MissShaderIndex
			ray,				// RayDesc
			shadowPayload		// payload_t
		);

		if (shadowPayload.hit)
			shadow_factor = 0.0;
	}

	float diffuse = 0.18;
	float ambient = 0.01;
	payload.color = diffuse * shadow_factor + ambient + InstanceDataBuffer[InstanceID()].mEmission;
}

[shader("closesthit")]
void shadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	payload.hit = true;
}

[shader("miss")]
void shadowMiss(inout ShadowPayload payload)
{
	payload.hit = false;
}
