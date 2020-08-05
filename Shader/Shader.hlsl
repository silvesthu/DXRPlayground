#include "Common.hlsl"

RWTexture2D<float4> RaytracingOutput : register(u0, space0);
cbuffer PerFrame : register(b0, space0)
{
	float4	mBackgroundColor;
	float4	mCameraPosition;
	float4	mCameraDirection;
	float4	mCameraRightExtend;
	float4	mCameraUpExtend;

	uint	mDebugMode;
	uint	mShadowMode;

	uint 	mRecursionCountMax;
	uint 	mFrameIndex;
	uint 	mAccumulationFrameCount;
}

RaytracingAccelerationStructure RaytracingScene : register(t0, space0);
struct InstanceData
{
	float3 mAlbedo;
	float3 mReflectance;
	float3 mEmission;
	float  mRoughness;

	uint   mIndexOffset;
	uint   mVertexOffset;
};
StructuredBuffer<InstanceData> InstanceDataBuffer : register(t1, space0);
ByteAddressBuffer Indices : register(t2, space0);
StructuredBuffer<float3> Vertices : register(t3, space0);
StructuredBuffer<float3> Normals : register(t4, space0);

struct RayPayload
{
	float3 color;
	uint recursion_depth;
};

struct ShadowPayload
{
	bool hit;
};

float3 linearToSrgb(float3 c)
{
	// Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	float3 sq1 = sqrt(c);
	float3 sq2 = sqrt(sq1);
	float3 sq3 = sqrt(sq2);
	float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
	return srgb;
}

// From D3D12Raytracing
// Load three 16 bit indices from a byte addressed buffer.
static
uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;
    const uint2 four16BitIndices = Indices.Load2(dwordAlignedOffset);

    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

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
	ray.Origin = mCameraPosition.xyz;
	ray.Direction = normalize(mCameraDirection.xyz + mCameraRightExtend.xyz * d.x + mCameraUpExtend.xyz * d.y);
	ray.TMin = 0;				// Near
	ray.TMax = 100000;			// Far

	RayPayload payload;
	payload.recursion_depth = 0;
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

	float3 current_frame_color = linearToSrgb(payload.color);
	float3 previous_frame_color = RaytracingOutput[launchIndex.xy].xyz;
	float3 mixed_color = lerp(previous_frame_color, current_frame_color, 1.0f / (float)(mAccumulationFrameCount));

	if (mDebugMode == 11)
		mixed_color = hsv2rgb(float3((payload.recursion_depth) * 1.0 / (mRecursionCountMax + 1), 1, 1));

	RaytracingOutput[launchIndex.xy] = float4(mixed_color, 1);
}

[shader("miss")]
void defaultMiss(inout RayPayload payload)
{
	payload.color = mBackgroundColor.xyz;
}

[shader("closesthit")]
void defaultClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	// See https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html for more system value intrinsics

	float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);

	// Get the base index of the triangle's first 16 bit index.
    uint index_size_in_bytes = 2;
    uint index_count_per_triangle = 3;
    uint triangleIndexStride = index_count_per_triangle * index_size_in_bytes;
    uint base_index = PrimitiveIndex() * triangleIndexStride + InstanceDataBuffer[InstanceID()].mIndexOffset * index_size_in_bytes;

    // Load up 3 16 bit indices for the triangle.
    const uint3 indices = Load3x16BitIndices(base_index);

    // Retrieve corresponding vertex normals for the triangle vertices.
    float3 normals[3] = { 
        Normals[indices[0]], 
        Normals[indices[1]], 
        Normals[indices[2]] 
    };

    float3 normal = normals[0] * barycentrics.x + normals[1] * barycentrics.y + normals[2] * barycentrics.z;

    // Retrieve corresponding vertex normals for the triangle vertices.
    float3 vertices[3] = { 
        Vertices[indices[0]], 
        Vertices[indices[1]], 
        Vertices[indices[2]] 
    };

    float3 vertex = vertices[0] * barycentrics.x + vertices[1] * barycentrics.y + vertices[2] * barycentrics.z;

    switch (mDebugMode)
    {
    	case 0: break;
    	case 1: break;
    	case 2: payload.color = barycentrics; return;
    	case 3: payload.color = vertex; return;
    	case 4: payload.color = normal; return;
    	case 5: break;
    	case 6: payload.color = InstanceDataBuffer[InstanceID()].mAlbedo; return;
    	case 7: payload.color = InstanceDataBuffer[InstanceID()].mReflectance; return;
    	case 8: payload.color = InstanceDataBuffer[InstanceID()].mEmission; return;
    	case 9: payload.color = InstanceDataBuffer[InstanceID()].mRoughness; return;
    	case 10: break;
    	case 11: break;
    	default:
    		break;
    }

	// Find the world-space hit position
	float3 hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

	if (payload.recursion_depth >= mRecursionCountMax)
	{
		payload.color = InstanceDataBuffer[InstanceID()].mEmission;
		return;
	}

	// Lambertian
	{
		// From https://www.shadertoy.com/view/tsBBWW
		uint random_state = uint(uint(DispatchRaysIndex().x) * uint(1973) + uint(DispatchRaysIndex().y) * uint(9277) + uint(mAccumulationFrameCount) * uint(26699)) | uint(1);

		float3 reflection_vector = 0;

		float3 random_vector = RandomUnitVector(random_state);
		if (dot(normal, random_vector) < 0)
			random_vector = -random_vector;
		reflection_vector = random_vector;

		// onb
		// {
		// 	float3 axis[3];			
		// 	axis[2] = normalize(normal);
		// 	float3 a = (abs(axis[2].x) > 0.9) ? float3(0, 1, 0) : float3(1, 0, 0);
		// 	axis[1] = normalize(cross(axis[2], a));
		// 	axis[0] = cross(axis[2], axis[1]);

		// 	float r1 = RandomFloat01(random_state);
		// 	float r2 = RandomFloat01(random_state);
		// 	float z = sqrt(1 - r2);

		// 	float phi = 2 * M_PI * r1;
		// 	float x = cos(phi) * sqrt(r2);
		// 	float y = sin(phi) * sqrt(r2);
		
		// 	float3 aa = float3(x, y, z);
		// 	reflection_vector = aa.x * axis[0] + aa.y * axis[1] + aa.z * axis[2];
		// }

		RayDesc ray;
		ray.Origin = hit_position;
		ray.Direction = reflection_vector;
		ray.TMin = 0.0001;			// Near
		ray.TMax = 100000;			// Far

		payload.recursion_depth += 1;

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

		payload.color = InstanceDataBuffer[InstanceID()].mAlbedo * payload.color + InstanceDataBuffer[InstanceID()].mEmission;

		return;
	}

	// Test
	{
		float shadow_factor = 1.0;
		if (mShadowMode == 1) // Test
		{
			// Fire a shadow ray. The direction is hard-coded here, but can be fetched from a constant-buffer
			RayDesc ray;
			ray.Origin = hit_position;
			ray.Direction = normalize(float3(-1, 1, -1));
			ray.TMin = 0.0001;
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
