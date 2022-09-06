#include "Constant.h"
#include "Shared.inl"
#include "Binding.h"

#ifndef SHADER_PROFILE_LIB
#define ENABLE_RAY_QUERY
#endif // SHADER_PROFILE_LIB

#include "Common.inl"
#include "RayQuery.inl"
#include "Planet.inl"
#include "AtmosphereIntegration.inl"
#include "CloudIntegration.inl"


[shader("miss")]
void DefaultMiss(inout RayPayload payload)
{
	payload.mState.Set(RayState::Done);

    float3 sky_luminance = GetSkyLuminance();

	float3 cloud_transmittance = 1;
    float3 cloud_luminance = 0;
    RaymarchCloud(cloud_transmittance, cloud_luminance);
	
    float3 emission = lerp(sky_luminance, cloud_luminance, 1.0 - cloud_transmittance);

	payload.mEmission = payload.mEmission + payload.mThroughput * emission;
}

HitInfo HitInternal(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{	
	HitInfo hit_info = (HitInfo)0;
	hit_info.mDone = true;

	// For more system value intrinsics, see https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html 
	float3 barycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);

	// Only support 32bit index, see https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl for reference
	uint index_count_per_triangle = 3;
    uint base_index = sGetPrimitiveIndex() * index_count_per_triangle + InstanceDatas[sGetInstanceID()].mIndexOffset;
	uint3 indices = uint3(Indices[base_index], Indices[base_index + 1], Indices[base_index + 2]);

	// Attributes
	float3 vertices[3] = { Vertices[indices[0]], Vertices[indices[1]], Vertices[indices[2]] };
	float3 vertex = vertices[0] * barycentrics.x + vertices[1] * barycentrics.y + vertices[2] * barycentrics.z;

	float3 normals[3] = { Normals[indices[0]], Normals[indices[1]], Normals[indices[2]] };
	float3 normal = normalize(normals[0] * barycentrics.x + normals[1] * barycentrics.y + normals[2] * barycentrics.z);
    normal = normalize(mul((float3x3) InstanceDatas[sGetInstanceID()].mInverseTranspose, normal)); // Allow non-uniform scale

	float2 uvs[3] = { UVs[indices[0]], UVs[indices[1]], UVs[indices[2]] };
	float2 uv = uvs[0] * barycentrics.x + uvs[1] * barycentrics.y + uvs[2] * barycentrics.z;

	// Handle front face only
	if (dot(normal, -sGetWorldRayDirection()) < 0)
	{
		bool flip_normal = false;
		if (flip_normal)
			normal = -normal;
		else
		{
			hit_info.mDone = true;
			return hit_info;
		}
	}

	// Hit position
	{
        float3 raw_hit_position = sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent();
        hit_info.mPosition = raw_hit_position + normal * 0.001;
    }

	// Participating Media
    float3 in_scattering = 0;
    float3 transmittance = 0;
    GetSkyLuminanceToPoint(in_scattering, transmittance);
	{
        hit_info.mInScattering = in_scattering;
		hit_info.mTransmittance = transmittance;
	}

	// Reflection
	{
		// TODO: Revisit. Support tangent ?
		// onb - build_from_w
		float3 axis[3];
		axis[2] = normal;
		float3 a = (abs(axis[2].x) > 0.9) ? float3(0, 1, 0) : float3(1, 0, 0);
		axis[1] = normalize(cross(axis[2], a));
		axis[0] = cross(axis[2], axis[1]);
		
		// Based on Mitsuba2
        switch (InstanceDatas[sGetInstanceID()].mMaterialType)
        {
            case MaterialType::Diffuse:
	            {
                    hit_info.mEmission = InstanceDatas[sGetInstanceID()].mEmission * (kEmissionBoostScale * kPreExposure);

            		// random
            		float3 direction = RandomCosineDirection(payload.mRandomState);

            		// onb - local
            		hit_info.mReflectionDirection = normalize(direction.x * axis[0] + direction.y * axis[1] + direction.z * axis[2]);

            		// pdf - exact distribution - should cancel out
            		float NdotL = dot(normal, hit_info.mReflectionDirection);
					hit_info.mScatteringPDF = NdotL <= 0 ? 0 : NdotL / MATH_PI;
            		hit_info.mSamplingPDF = NdotL <= 0 ? 0 : NdotL / MATH_PI;
                    hit_info.mAlbedo = InstanceDatas[sGetInstanceID()].mAlbedo;
            		hit_info.mDone = false;
	            }
                break;
            case MaterialType::RoughConductor:
				{
                    hit_info.mEmission = InstanceDatas[sGetInstanceID()].mEmission * (kEmissionBoostScale * kPreExposure);

            		// TODO: Check visible normal

                    float a = InstanceDatas[sGetInstanceID()].mRoughnessAlpha;
            		float a2 = a * a;

            		// Microfacet
            		float3 H; // Microfacet normal (Half-vector)
            		{                    	
						float e0 = RandomFloat01(payload.mRandomState);
                    	float e1 = RandomFloat01(payload.mRandomState);
                    	
                    	// 2D Distribution -> GGX Distribution (Polar)
                    	float cos_theta = sqrt((1.0 - e0) / ((a2 - 1) * e0 + 1.0));
                    	float sin_theta = sqrt( 1 - cos_theta * cos_theta );
                    	float phi = 2 * MATH_PI * e1;

                    	// Polar -> Cartesian
                    	H.x = sin_theta * cos(phi);
                    	H.y = sin_theta * sin(phi);
                    	H.z = cos_theta;

                    	// Tangent -> World
                    	H = normalize(H.x * axis[0] + H.y * axis[1] + H.z * axis[2]);
            		}

            		// TODO: Better to calculate in tangent space?
            		float3 V = -sGetWorldRayDirection();
            		float HdotV = dot(H, V);
            		float3 L = 2.0 * HdotV * H - V;
            		float NdotH = dot(normal, H);
            		float NdotV = dot(normal, V);
            		float HdotL = dot(H, L);
            		float NdotL = dot(normal, L);

            		float G = G_SmithGGX(NdotL, NdotV, a2);
                    float3 F = F_Schlick(InstanceDatas[sGetInstanceID()].mReflectance, HdotV);

            		hit_info.mReflectionDirection = L;
            		hit_info.mAlbedo = G * F / (4.0f * NdotV);
            		hit_info.mScatteringPDF = 1.0;
            		hit_info.mSamplingPDF = NdotH / (4.0f * abs(HdotL));
            		hit_info.mDone = false;
                }
                break;
			default:
                break;
        }
		
		//if (!payload.mState.IsSet(RayState::FirstHit))
		//	hit_info.mEmission = 0;
	}

	// Debug - Global
	{
		bool terminate = true;
		switch (mConstants.mDebugMode)
		{
		case DebugMode::Barycentrics: 			hit_info.mEmission = barycentrics; break;
		case DebugMode::Vertex: 				hit_info.mEmission = vertex; break;
		case DebugMode::Normal: 				hit_info.mEmission = normal * 0.5 + 0.5; break;
		case DebugMode::UV:						hit_info.mEmission = float3(uv, 0.0); break;
		case DebugMode::Albedo: 				hit_info.mEmission = InstanceDatas[sGetInstanceID()].mAlbedo; break;
		case DebugMode::Reflectance: 			hit_info.mEmission = InstanceDatas[sGetInstanceID()].mReflectance; break;
		case DebugMode::Emission: 				hit_info.mEmission = InstanceDatas[sGetInstanceID()].mEmission; break;
		case DebugMode::RoughnessAlpha: 		hit_info.mEmission = InstanceDatas[sGetInstanceID()].mRoughnessAlpha; break;
		case DebugMode::Transmittance:			hit_info.mEmission = transmittance; break;
		case DebugMode::InScattering:			hit_info.mEmission = in_scattering; break;
		case DebugMode::RecursionCount:			terminate = false; break;
		case DebugMode::RussianRouletteCount:	terminate = false; break;
		default:								terminate = false; break;
		}

		if (terminate)
		{
			hit_info.mDone = true;
			return hit_info;
		}
	}

	// Debug - Per instance
    if (mConstants.mDebugInstanceIndex == -1 || mConstants.mDebugInstanceIndex == sGetInstanceID())
	{
		switch (mConstants.mDebugInstanceMode)
		{
		case DebugInstanceMode::Barycentrics:
			hit_info.mEmission = barycentrics; 
			hit_info.mDone = true; 
			return hit_info;
		case DebugInstanceMode::Mirror:
			hit_info.mAlbedo = 1; 
            hit_info.mSamplingPDF = 1;
            hit_info.mScatteringPDF = 1;
			hit_info.mReflectionDirection = reflect(sGetWorldRayDirection(), normal); 
			hit_info.mDone = false; 
			return hit_info;
		default:
			break;
		}
	}

	return hit_info;
}

[shader("closesthit")]
void DefaultClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	HitInfo hit_info = HitInternal(payload, attributes);

	// State
	payload.mState.Set(hit_info.mDone ? RayState::Done : RayState::None);

	// Geometry
	payload.mPosition = hit_info.mPosition;
	payload.mReflectionDirection = hit_info.mReflectionDirection;

	// Material
	payload.mEmission = payload.mEmission + payload.mThroughput * hit_info.mTransmittance * hit_info.mEmission + hit_info.mInScattering;
	payload.mThroughput = hit_info.mSamplingPDF <= 0 ? 0 : payload.mThroughput * hit_info.mAlbedo * hit_info.mTransmittance * hit_info.mScatteringPDF / hit_info.mSamplingPDF;
}

void TraceRay()
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
	ray.TMin = 0.001;				// Near
	ray.TMax = 100000;				// Far

	RayPayload payload = (RayPayload)0;
	payload.mThroughput = 1; // Camera gather all the light
	payload.mRandomState = uint(uint(sGetDispatchRaysIndex().x) * uint(1973) + uint(sGetDispatchRaysIndex().y) * uint(9277) + uint(mConstants.mAccumulationFrameCount) * uint(26699)) | uint(1); // From https://www.shadertoy.com/view/tsBBWW
	payload.mState.Reset(RayState::FirstHit);

#ifdef ENABLE_RAY_QUERY
	// Note that RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH will give first hit for "Any Hit". The result may not be the closest one
	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	uint additional_ray_flags = 0;
	uint ray_instance_mask = 0xffffffff;
#endif // ENABLE_RAY_QUERY

	RaytracingAccelerationStructure RaytracingScene = ResourceDescriptorHeap[(int) ViewDescriptorIndex::RaytraceTLASSRV];
	uint recursion = 0;
	for (;;)
	{
#ifdef ENABLE_RAY_QUERY
		sWorldRayOrigin 		= ray.Origin;
		sWorldRayDirection		= ray.Direction;

		query.TraceRayInline(RaytracingScene, additional_ray_flags, ray_instance_mask, ray);
		query.Proceed();

		if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			sRayTCurrent		= query.CommittedRayT();
			sInstanceIndex 		= query.CommittedInstanceIndex();
			sPrimitiveIndex 	= query.CommittedPrimitiveIndex();
			sGeometryIndex 		= query.CommittedGeometryIndex();
			sInstanceID 		= query.CommittedInstanceID();

			BuiltInTriangleIntersectionAttributes attributes;
			attributes.barycentrics = query.CommittedTriangleBarycentrics();

			DefaultClosestHit(payload, attributes);
		}
		else
		{
			DefaultMiss(payload);
		}
#else
		TraceRay(
			RaytracingScene, 		// RaytracingAccelerationStructure
			0,						// RayFlags 
			0xFF,					// InstanceInclusionMask
			0,						// RayContributionToHitGroupIndex, 4bits
			0,						// MultiplierForGeometryContributionToHitGroupIndex, 16bits
			0,						// MissShaderIndex
			ray,					// RayDesc
			payload					// payload_t
		);
#endif // ENABLE_RAY_QUERY

		if (payload.mState.IsSet(RayState::Done))
			break;

		// Russian Roulette
		// [TODO] Make a test scene for comparison
		// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting.html
		// https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer
		if (recursion >= mConstants.mRecursionCountMax)
		{
			if (mConstants.mRecursionMode == RecursionMode::RussianRoulette && recursion <= 8)
			{
				// Probability can be chosen in almost any manner
				// e.g. Fixed threshold
				// e.g. Veach's Efficiency-Optimized Russian roulette is based on average variance and cost
				// Based on throughput here (basically albedo)
				float3 throughput = payload.mThroughput;
				float termination_probability = max(0.25, 1.0 - max(throughput.x, max(throughput.y, throughput.z)));

				if (RandomFloat01(payload.mRandomState) < termination_probability)
					break;

				// Weight the sample to keep result unbiased
				payload.mThroughput /= (1 - termination_probability);
			}
			else
				break;
		}
		
		ray.Origin = payload.mPosition;
		ray.Direction = payload.mReflectionDirection;
		
		payload.mState.Unset(RayState::FirstHit);

		recursion++;
	}

	// Accumulation
	{
		float3 current_output = payload.mEmission;

		float3 previous_output = RaytracingOutput[sGetDispatchRaysIndex().xy].xyz;
		previous_output = max(0, previous_output); // Eliminate nan
		float3 mixed_output = lerp(previous_output, current_output, 1.0f / (float)(mConstants.mAccumulationFrameCount));

		if (recursion == 0)
			mixed_output = current_output;

		if (mConstants.mDebugMode == DebugMode::RecursionCount)
			mixed_output = hsv2rgb(float3(recursion * 1.0 / (mConstants.mRecursionCountMax + 1), 1, 1));

		if (mConstants.mDebugMode == DebugMode::RussianRouletteCount)
			mixed_output = hsv2rgb(float3(max(0.0, (recursion * 1.0 - mConstants.mRecursionCountMax * 1.0)) / 10.0 /* for visualization only */, 1, 1));

		RaytracingOutput[sGetDispatchRaysIndex().xy] = float4(mixed_output, 1);
	}
}

[shader("raygeneration")]
void DefaultRayGeneration()
{
	TraceRay();
}

#ifdef ENABLE_RAY_QUERY
[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void RayQueryCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	uint2 output_dimensions;
	RaytracingOutput.GetDimensions(output_dimensions.x, output_dimensions.y);

	sDispatchRaysIndex.xyz = inDispatchThreadID.xyz;
	sDispatchRaysDimensions = uint3(output_dimensions.xy, 1);

	TraceRay();
}
#endif // ENABLE_RAY_QUERY