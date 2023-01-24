#include "Constant.h"
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Material.h"
#include "RayQuery.h"
#include "Planet.h"
#include "AtmosphereIntegration.h"
#include "CloudIntegration.h"

float3 sGetWorldRayHitPosition()
{
	return sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent();
}

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
	// System value intrinsics https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#system-value-intrinsics

	HitInfo hit_info = (HitInfo)0;
	hit_info.mDone = true;

	float3 barycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);

	// Only support 32bit index for simplicity
	// see https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl for reference
	uint index_count_per_triangle = 3;
    uint base_index = sGetPrimitiveIndex() * index_count_per_triangle + InstanceDatas[sGetInstanceID()].mIndexOffset;
	uint3 indices = uint3(Indices[base_index], Indices[base_index + 1], Indices[base_index + 2]);

	// Vertex attributes
	float3 vertices[3] = { Vertices[indices[0]], Vertices[indices[1]], Vertices[indices[2]] };
	float3 vertex = vertices[0] * barycentrics.x + vertices[1] * barycentrics.y + vertices[2] * barycentrics.z;

	float3 normals[3] = { Normals[indices[0]], Normals[indices[1]], Normals[indices[2]] };
	float3 normal = normalize(normals[0] * barycentrics.x + normals[1] * barycentrics.y + normals[2] * barycentrics.z);
    normal = normalize(mul((float3x3) InstanceDatas[sGetInstanceID()].mInverseTranspose, normal)); // Allow non-uniform scale

	float2 uvs[3] = { UVs[indices[0]], UVs[indices[1]], UVs[indices[2]] };
	float2 uv = uvs[0] * barycentrics.x + uvs[1] * barycentrics.y + uvs[2] * barycentrics.z;

	// Handle back face
	if (dot(normal, -sGetWorldRayDirection()) < 0)
	{
		if (InstanceDatas[sGetInstanceID()].mTwoSided)
			normal = -normal;
		else
			return hit_info;
	}

	// Hit position
	{
		hit_info.mPosition = sGetWorldRayHitPosition() + normal * 0.001;
	}

	// Participating media
	{
		GetSkyLuminanceToPoint(hit_info.mInScattering, hit_info.mTransmittance);
	}
	
	// Emission
	{
		hit_info.mEmission = InstanceDatas[sGetInstanceID()].mEmission * (kEmissionBoostScale * kPreExposure);
	}

	// Reflection / Refraction
	hit_info.mDone = InstanceDatas[sGetInstanceID()].mMaterialType == MaterialType::Light;
	if (!hit_info.mDone)
	{		
		// Generate next sample based on material
		float3 material_sample = MaterialEvaluation::GenerateSample(normal, payload);
		float material_pdf = MaterialEvaluation::ComputePDF(material_sample, normal);
		MaterialEvaluation::Evaluate(material_sample, normal, hit_info);
	}

	// Debug - Global
	switch (mConstants.mDebugMode)
	{
	case DebugMode::None:						break;
	case DebugMode::Barycentrics: 				hit_info.mEmission = barycentrics; hit_info.mDone = true; break;
	case DebugMode::Vertex: 					hit_info.mEmission = vertex; hit_info.mDone = true; break;
	case DebugMode::Normal: 					hit_info.mEmission = normal * 0.5 + 0.5; hit_info.mDone = true; break;
	case DebugMode::UV:							hit_info.mEmission = float3(uv, 0.0); hit_info.mDone = true; break;
	case DebugMode::Albedo: 					hit_info.mEmission = InstanceDatas[sGetInstanceID()].mAlbedo; hit_info.mDone = true; break;
	case DebugMode::Reflectance: 				hit_info.mEmission = InstanceDatas[sGetInstanceID()].mReflectance; hit_info.mDone = true; break;
	case DebugMode::Emission: 					hit_info.mEmission = InstanceDatas[sGetInstanceID()].mEmission; hit_info.mDone = true; break;
	case DebugMode::RoughnessAlpha: 			hit_info.mEmission = InstanceDatas[sGetInstanceID()].mRoughnessAlpha; hit_info.mDone = true; break;
	case DebugMode::Transmittance:				hit_info.mEmission = hit_info.mTransmittance; hit_info.mDone = true; break;
	case DebugMode::InScattering:				hit_info.mEmission = hit_info.mInScattering; hit_info.mDone = true; break;
	case DebugMode::RecursionCount:				hit_info.mDone = false; break;
	case DebugMode::RussianRouletteCount:		hit_info.mDone = false; break;
	default:									hit_info.mEmission = 0; hit_info.mDone = true; break;
	}

	// Debug - Per-instance
    if (mConstants.mDebugInstanceIndex == sGetInstanceID())
	{
		switch (mConstants.mDebugInstanceMode)
		{
		case DebugInstanceMode::Barycentrics:	hit_info.mEmission = barycentrics; hit_info.mDone = true; break;
		case DebugInstanceMode::Mirror:			hit_info.mReflectionDirection = reflect(sGetWorldRayDirection(), normal); hit_info.mBSDF = 1; hit_info.mNdotL = dot(normal, hit_info.mReflectionDirection); hit_info.mSamplingPDF = 1; hit_info.mDone = false; break;
		default: break;
		}
	}

	return hit_info;
}

void DefaultClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	HitInfo hit_info = HitInternal(payload, attributes);

	// State
	payload.mState.Set(hit_info.mDone ? RayState::Done : RayState::None);

	// Emission
	payload.mEmission = payload.mEmission + payload.mThroughput * hit_info.mTransmittance * hit_info.mEmission + hit_info.mInScattering;
	
	// Next bounce
	payload.mPosition = hit_info.mPosition;	
	payload.mReflectionDirection = hit_info.mReflectionDirection;
	payload.mThroughput = hit_info.mSamplingPDF <= 0 ? 0 : (payload.mThroughput * hit_info.mTransmittance * hit_info.mBSDF * hit_info.mNdotL / hit_info.mSamplingPDF);
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
	ray.TMin = 0.001;
	ray.TMax = 100000;

	RayPayload payload = (RayPayload)0;
	payload.mThroughput = 1; // Camera gather all the light
	payload.mRandomState = uint(uint(sGetDispatchRaysIndex().x) * uint(1973) + uint(sGetDispatchRaysIndex().y) * uint(9277) + uint(mConstants.mCurrentFrameIndex) * uint(26699)) | uint(1); // From https://www.shadertoy.com/view/tsBBWW
	payload.mState.Reset(RayState::FirstHit);

	// Note that RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH will give first hit for "Any Hit". The result may not be the closest one
	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	uint additional_ray_flags = 0;
	uint ray_instance_mask = 0xffffffff;

	uint recursion = 0;
	for (;;)
	{
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
		float3 mixed_output = lerp(previous_output, current_output, mConstants.mCurrentFrameWeight);

		if (recursion == 0)
			mixed_output = current_output;

		if (mConstants.mDebugMode == DebugMode::RecursionCount)
			mixed_output = hsv2rgb(float3(recursion * 1.0 / (mConstants.mRecursionCountMax + 1), 1, 1));

		if (mConstants.mDebugMode == DebugMode::RussianRouletteCount)
			mixed_output = hsv2rgb(float3(max(0.0, (recursion * 1.0 - mConstants.mRecursionCountMax * 1.0)) / 10.0 /* for visualization only */, 1, 1));

		RaytracingOutput[sGetDispatchRaysIndex().xy] = float4(mixed_output, 1);
	}
}

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