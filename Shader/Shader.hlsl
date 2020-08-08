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
	uint 	mDebugInstanceMode;
	uint 	mDebugInstanceIndex;
	uint	mShadowMode;

	uint 	mRecursionCountMax;
	uint 	mFrameIndex;
	uint 	mAccumulationFrameCount;

	uint 	mReset;

	uint2   mDebugCoord;
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

#define DEBUG_PIXEL_RADIUS (3)

struct RayPayload
{
	float3 mColor;
	uint mRandomState;
	uint mRecursionDepth;
};

struct ShadowPayload
{
	bool hit;
};

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
void DefaultRayGeneration()
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
	payload.mRecursionDepth = 0;
	payload.mRandomState = uint(uint(DispatchRaysIndex().x) * uint(1973) + uint(DispatchRaysIndex().y) * uint(9277) + uint(mAccumulationFrameCount) * uint(26699)) | uint(1); // From https://www.shadertoy.com/view/tsBBWW
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

	float3 current_frame_color = payload.mColor;
	float3 previous_frame_color = RaytracingOutput[DispatchRaysIndex().xy].xyz;
	float3 mixed_color = lerp(previous_frame_color, current_frame_color, 1.0f / (float)(mAccumulationFrameCount));

	if (mDebugMode == 11)
		mixed_color = hsv2rgb(float3((payload.mRecursionDepth) * 1.0 / (mRecursionCountMax + 1), 1, 1));

	// [TODO] Ray visualization ?
	// if (all(abs((int2)DispatchRaysIndex().xy - (int2)mDebugCoord) < DEBUG_PIXEL_RADIUS))
	// 	mixed_color = 0;

	RaytracingOutput[DispatchRaysIndex().xy] = float4(mixed_color, 1);
}

[shader("miss")]
void DefaultMiss(inout RayPayload payload)
{
	payload.mColor = mBackgroundColor.xyz;
}

[shader("closesthit")]
void DefaultClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
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
    	case 2: payload.mColor = barycentrics; return;
    	case 3: payload.mColor = vertex; return;
    	case 4: payload.mColor = normal * 0.5 + 0.5; return;
    	case 5: break;
    	case 6: payload.mColor = InstanceDataBuffer[InstanceID()].mAlbedo; return;
    	case 7: payload.mColor = InstanceDataBuffer[InstanceID()].mReflectance; return;
    	case 8: payload.mColor = InstanceDataBuffer[InstanceID()].mEmission; return;
    	case 9: payload.mColor = InstanceDataBuffer[InstanceID()].mRoughness; return;
    	case 10: break;
    	case 11: break;
    	default:
    		break;
    }

	// Find the world-space hit position
	float3 hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

	if (payload.mRecursionDepth >= mRecursionCountMax)
	{
		payload.mColor = InstanceDataBuffer[InstanceID()].mEmission;
		return;
	}

	if (dot(normal, -WorldRayDirection()) < 0)
	{
		payload.mColor = 0;
		return;
	}

	// Lambertian
	{
		float3 origin = hit_position + normal * 0.001;
		float3 direction = 0;

		// random
		{
			float3 random_vector = RandomUnitVector(payload.mRandomState);
			if (dot(normal, random_vector) < 0)
				random_vector = -random_vector;
			direction = random_vector;
		}

		// onb
		{
			// float3 axis[3];			
			// axis[2] = normalize(normal);
			// float3 a = (abs(axis[2].x) > 0.9) ? float3(0, 1, 0) : float3(1, 0, 0);
			// axis[1] = normalize(cross(axis[2], a));
			// axis[0] = cross(axis[2], axis[1]);

			// float r1 = RandomFloat01(random_state);
			// float r2 = RandomFloat01(random_state);
			// float z = sqrt(1 - r2);

			// float phi = 2 * M_PI * r1;
			// float x = cos(phi) * sqrt(r2);
			// float y = sin(phi) * sqrt(r2);
		
			// float3 aa = float3(x, y, z);
			// direction = aa.x * axis[0] + aa.y * axis[1] + aa.z * axis[2];
		}

		if (mDebugInstanceIndex == InstanceID())
		{
			switch (mDebugInstanceMode)
			{
				case 1: payload.mColor = barycentrics; return;						// Barycentrics
				case 2: direction = reflect(WorldRayDirection(), normal); break; 	// Mirror
				default: break;
			}
		}

		RayDesc ray;
		ray.Origin = origin;
		ray.Direction = direction;
		ray.TMin = 0.0001;			// Near
		ray.TMax = 100000;			// Far

		uint current_recursion_depth = payload.mRecursionDepth;
		payload.mRecursionDepth += 1;

		// Should this be a recursive call or a loop ?
		// - optixPathTracer use a loop with Ray in Payload
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

		payload.mColor = InstanceDataBuffer[InstanceID()].mAlbedo * payload.mColor + InstanceDataBuffer[InstanceID()].mEmission;

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
		payload.mColor = diffuse * shadow_factor + ambient + InstanceDataBuffer[InstanceID()].mEmission;
	}
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	payload.hit = true;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
	payload.hit = false;
}

float4 CopyTextureVS(uint id : SV_VertexID) : SV_POSITION
{
	// From https://anteru.net/blog/2012/minimal-setup-screen-space-quads-no-buffers-layouts-required/
	// Generate screen space triangle
	float x = float ((id & 2) << 1) - 1.0;
	float y = 1.0 - float ((id & 1) << 2);
	return float4 (x, y, 0, 1);
}

Texture2D<float4> CopyFromTexture : register(t0, space1);
[RootSignature("DescriptorTable(SRV(t0, numDescriptors = 1, space = 1), visibility = SHADER_VISIBILITY_PIXEL)")]
float4 CopyTexturePS(float4 position : SV_POSITION) : SV_TARGET
{
	float3 srgb = ApplySRGBCurve(CopyFromTexture.Load(int3(position.xy, 0)));
	return float4(srgb, 1);
}