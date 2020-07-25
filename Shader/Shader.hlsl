RaytracingAccelerationStructure RaytracingScene : register(t0);
RWTexture2D<float4> RaytracingOutput : register(u0);

cbuffer PerFrame : register(b0)
{
	float4 BackgroundColor;
	float4 CameraPosition;
	float4 CameraDirection;
	float4 CameraRightExtend;
	float4 CameraUpExtend;
}

cbuffer PerScene : register(b1)
{
	float3 A[3];
	float3 B[3];
	float3 C[3];
}

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

[shader("raygeneration")]
void rayGen()
{
	uint3 launchIndex = DispatchRaysIndex();
	uint3 launchDim = DispatchRaysDimensions();

	float2 crd = float2(launchIndex.xy);
	float2 dims = float2(launchDim.xy);

	float2 d = ((crd/dims) * 2.f - 1.f); // 0~1 => -1~1
	
	RayDesc ray;
	ray.Origin = CameraPosition;
	ray.Direction = normalize(CameraDirection + CameraRightExtend * d.x + CameraUpExtend * d.y);
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
void miss(inout RayPayload payload)
{
	payload.color = BackgroundColor;
}

[shader("closesthit")]
void triangleHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	// BuiltInTriangleIntersectionAttributes: hit attributes for fixed-function triangle intersection

	uint instanceID = InstanceID();
	float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
	payload.color = A[instanceID] * barycentrics.x + B[instanceID] * barycentrics.y + C[instanceID] * barycentrics.z;
}

struct ShadowPayload
{
	bool hit;
};

[shader("closesthit")]
void planeHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	// See https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html for more system value intrinsics

	// Find the world-space hit position
	float3 hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

	// Fire a shadow ray. The direction is hard-coded here, but can be fetched from a constant-buffer
	RayDesc ray;
	ray.Origin = hit_position;
	ray.Direction = normalize(float3(-1, 1, -1));
	ray.TMin = 0.01;
	ray.TMax = 100000;

	// See also https://sites.google.com/site/monshonosuana/directxno-hanashi-1/directx-153
	// HitGroup Shader Index in Shader Table = 
	//	InstanceContributionToHitGroupIndex + RayContributionToHitGroupIndex + (GeometryContributionToHitGroupIndex * MultiplierForGeometryContributionToHitGroupIndex)
	//
	// InstanceContributionToHitGroupIndex: TLAS - basically for material
	// RayContributionToHitGroupIndex: RayDesc - basically for nth ray
	// GeometryContribution: BLAS - automatically assigned
	// MultiplierForGeometryContributionToHitGroupIndex: RayDesc - maybe same BLAS but different material set?

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

	float shadow_factor = shadowPayload.hit ? 0.0 : 1.0;
	float diffuse = 0.18;
	float ambient = 0.01;
	payload.color = diffuse * shadow_factor + ambient;
}

[shader("closesthit")]
void shadowHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	payload.hit = true;
}

[shader("miss")]
void shadowMiss(inout ShadowPayload payload)
{
	payload.hit = false;
}
