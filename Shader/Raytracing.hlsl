// [TODO]
// - MIS
// - HDRI

#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "BSDF.h"
#include "Light.h"
#include "RayQuery.h"
#include "Planet.h"
#include "AtmosphereIntegration.h"
#include "CloudIntegration.h"

static float3 sDebugOutput = 0;
void DebugOutput(DebugMode inDebugMode, float3 inValue)
{
	if (mConstants.mDebugMode == inDebugMode)
		sDebugOutput = inValue;
}

void DebugValue(PixelDebugMode inPixelDebugMode, uint inRecursionCount, float4 inValue)
{
	if (mConstants.mPixelDebugMode == inPixelDebugMode)
		if (sGetDispatchRaysIndex().x == mConstants.mPixelDebugCoord.x && sGetDispatchRaysIndex().y == mConstants.mPixelDebugCoord.y && inRecursionCount < Debug::kValueArraySize)
			BufferDebugUAV[0].mValueArray[inRecursionCount] = inValue;
}

void TraceRay()
{
	uint3 launchIndex = sGetDispatchRaysIndex();
	uint3 launchDim = sGetDispatchRaysDimensions();

	float2 crd = float2(launchIndex.xy) + 0.5;
	float2 dims = float2(launchDim.xy);

	float2 d = ((crd / dims) * 2.f - 1.f); // (0,1) => (-1,1)
	d.y = -d.y;
	
	RayDesc ray;
	ray.Origin = mConstants.mCameraPosition.xyz;
	ray.Direction = normalize(mConstants.mCameraDirection.xyz + mConstants.mCameraRightExtend.xyz * d.x + mConstants.mCameraUpExtend.xyz * d.y);
	ray.TMin = 0.001;
	ray.TMax = 100000;

	PathContext path_context = (PathContext)0;
	path_context.mThroughput = 1; // Camera gather all the light
	path_context.mPrevBSDFPDF = 0;
	path_context.mRandomState = uint(uint(sGetDispatchRaysIndex().x) * uint(1973) + uint(sGetDispatchRaysIndex().y) * uint(9277) + uint(mConstants.mCurrentFrameIndex) * uint(26699)) | uint(1); // From https://www.shadertoy.com/view/tsBBWW
	path_context.mRecursionCount = 0;

	// Note that RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH will give first hit for "Any Hit". The result may not be the closest one
	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	uint additional_ray_flags = 0;
	uint ray_instance_mask = 0xffffffff;

	for (;;)
	{
		bool continue_bounce				= false;
		float3 light_emission				= 0;

		sWorldRayOrigin 					= ray.Origin;
		sWorldRayDirection					= ray.Direction;

		query.TraceRayInline(RaytracingScene, additional_ray_flags, ray_instance_mask, ray);
		query.Proceed();

		if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			sRayTCurrent					= query.CommittedRayT();

			BuiltInTriangleIntersectionAttributes attributes;
			attributes.barycentrics			= query.CommittedTriangleBarycentrics();

			HitContext hit_context			= (HitContext)0;

			hit_context.mInstanceID			= query.CommittedInstanceID();
			hit_context.mPrimitiveIndex		= query.CommittedPrimitiveIndex();

			hit_context.mRayOriginWS		= ray.Origin;
			hit_context.mRayDirectionWS		= ray.Direction;

			// System value intrinsics https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#system-value-intrinsics

			// Vertex attributes
			{
				hit_context.mBarycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);

				// Only support 32bit index for simplicity
				// see https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl for reference
				uint index_count_per_triangle = 3;
				uint base_index = hit_context.mPrimitiveIndex * index_count_per_triangle + InstanceDatas[hit_context.mInstanceID].mIndexOffset;
				uint3 indices = uint3(Indices[base_index], Indices[base_index + 1], Indices[base_index + 2]);

				float3 vertices[3] = { Vertices[indices[0]], Vertices[indices[1]], Vertices[indices[2]] };
				hit_context.mVertexPositionOS = vertices[0] * hit_context.mBarycentrics.x + vertices[1] * hit_context.mBarycentrics.y + vertices[2] * hit_context.mBarycentrics.z;

				float3 normals[3] = { Normals[indices[0]], Normals[indices[1]], Normals[indices[2]] };
				float3 normal = normalize(normals[0] * hit_context.mBarycentrics.x + normals[1] * hit_context.mBarycentrics.y + normals[2] * hit_context.mBarycentrics.z);
				hit_context.mVertexNormalOS = normal;
				hit_context.mVertexNormalWS = normalize(mul((float3x3) InstanceDatas[hit_context.mInstanceID].mInverseTranspose, normal)); // Allow non-uniform scale

				// Handle back face
				if (dot(hit_context.mVertexNormalWS, -sGetWorldRayDirection()) < 0)
				{
					if (InstanceDatas[hit_context.mInstanceID].mTwoSided)
						hit_context.mVertexNormalWS = -hit_context.mVertexNormalWS;
					else
						break;
				}

				float2 uvs[3] = { UVs[indices[0]], UVs[indices[1]], UVs[indices[2]] };
				hit_context.mUV = uvs[0] * hit_context.mBarycentrics.x + uvs[1] * hit_context.mBarycentrics.y + uvs[2] * hit_context.mBarycentrics.z;
			}

			// Hit position
			{
				hit_context.mHitPositionWS = (sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent());

				// Debug
				DebugValue(PixelDebugMode::PositionWS, path_context.mRecursionCount, float4(hit_context.mHitPositionWS, 0));
			}

			// Participating media
			{
				float3 in_scattering = 0;
				float3 transmittance = 1;
				GetSkyLuminanceToPoint(in_scattering, transmittance);

				path_context.mEmission += in_scattering;
				path_context.mThroughput *= transmittance;

				DebugOutput(DebugMode::InScattering, in_scattering);
				DebugOutput(DebugMode::Transmittance, transmittance);
			}

			// Emission
			float3 emission = InstanceDatas[hit_context.mInstanceID].mEmission * (kEmissionBoostScale * kPreExposure);
			{
				// IES
				//float angle = acos(dot(-sGetWorldRayDirection(), float3(0,-1,0))) / MATH_PI;
				//float ies = IESSRV.SampleLevel(BilinearClampSampler, float2(angle, 0), 0).x;
				//hit_context.mEmission *= ies;

				if (mConstants.mDebugInstanceIndex == hit_context.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
					emission = hit_context.mBarycentrics;
			}

			// Lighting
			if (InstanceDatas[hit_context.mInstanceID].mBSDFType == BSDFType::Light)
			{
				// Direct Lighting
				if (path_context.mRecursionCount == 0 || mConstants.mLightCount == 0 || mConstants.mSampleMode == SampleMode::SampleBSDF)
				{
					path_context.mEmission += path_context.mThroughput * emission;
				}
				else if (mConstants.mSampleMode == SampleMode::MIS)
				{
					Light light = Lights[InstanceDatas[hit_context.mInstanceID].mLightID];

					float light_sample_pdf = 0;
					float3 light_sample_direction = 0;
					LightEvaluation::GenerateSamplingDirection(light, ray.Origin, path_context, light_sample_direction, light_sample_pdf);

					DebugValue(PixelDebugMode::LightPDFForBSDF, path_context.mRecursionCount, float4(light_sample_pdf.xxx, 0));

					float light_pdf = light_sample_pdf * LightEvaluation::GetLightSelectionPDF();
					float mis_weight = max(0.0, MIS::PowerHeuristic(1, path_context.mPrevBSDFPDF, 1, light_pdf));
					path_context.mEmission += path_context.mThroughput * emission * mis_weight;
				}
			}
			else
			{
				// Sample Light
				bool sample_light = mConstants.mSampleMode == SampleMode::SampleLight || mConstants.mSampleMode == SampleMode::MIS;
				if (sample_light && mConstants.mLightCount > 0)
				{
					uint light_index = min(RandomFloat01(path_context.mRandomState) * mConstants.mLightCount, mConstants.mLightCount - 1);
					Light light = Lights[light_index];					

					float light_sample_pdf = 0;
					float3 light_sample_direction = 0;
					LightEvaluation::GenerateSamplingDirection(light, hit_context.mHitPositionWS, path_context, light_sample_direction, light_sample_pdf);

					if (mConstants.mDebugInstanceIndex == hit_context.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Mirror)
						light_sample_direction = -reflect(-hit_context.mRayDirectionWS, hit_context.mVertexNormalWS) * 1E6; // long distance

					DebugValue(PixelDebugMode::LightPDFForLight, path_context.mRecursionCount, float4(light_sample_pdf.xxx, 0));

					float light_pdf = light_sample_pdf * LightEvaluation::GetLightSelectionPDF();
					if (light_pdf > 0)
					{
						RayDesc shadow_ray;
						shadow_ray.Origin = hit_context.mHitPositionWS;
						shadow_ray.Direction = normalize(light_sample_direction);
						shadow_ray.TMin = 0.001;
						shadow_ray.TMax = 1E6; // long distance

						RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> shadow_query;
						uint additional_shadow_ray_flags = 0;
						uint shadow_ray_instance_mask = 0xffffffff;

						shadow_query.TraceRayInline(RaytracingScene, additional_shadow_ray_flags, shadow_ray_instance_mask, shadow_ray);
						shadow_query.Proceed();

						if (shadow_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT && shadow_query.CommittedInstanceID() == light.mInstanceID)
						{
							BSDFContext bsdf_context = BSDFEvaluation::GenerateContext(shadow_ray.Direction, hit_context.mVertexNormalWS, -hit_context.mRayDirectionWS, hit_context);
							BSDFEvaluation::SampleDirection(hit_context, bsdf_context);

							float3 luminance = light.mEmission * (kEmissionBoostScale * kPreExposure);
							float3 emission = luminance * bsdf_context.mBSDF * bsdf_context.mNdotL / light_pdf;

							if (mConstants.mSampleMode == SampleMode::MIS)
								emission *= max(0.0, MIS::PowerHeuristic(1, light_pdf, 1, bsdf_context.mBSDFPDF));

							light_emission = path_context.mThroughput * emission;
						}
					}
				}

				// Sample BSDF
				{
					// Generate next sample based on BSDF
					float3 importance_sampling_direction = BSDFEvaluation::GenerateImportanceSamplingDirection(hit_context.mVertexNormalWS, hit_context, path_context);
					BSDFContext bsdf_context = BSDFEvaluation::GenerateContext(importance_sampling_direction, hit_context.mVertexNormalWS, -hit_context.mRayDirectionWS, hit_context);
					BSDFEvaluation::SampleBSDF(hit_context, bsdf_context);

					path_context.mEmission += path_context.mThroughput * emission; // Non-light emission
					path_context.mThroughput *= bsdf_context.mBSDFPDF <= 0 ? 0 : (bsdf_context.mBSDF * bsdf_context.mNdotL / bsdf_context.mBSDFPDF);
					path_context.mPrevBSDFPDF = bsdf_context.mBSDFPDF;

					// Next bounce
					ray.Origin = hit_context.mHitPositionWS;
					ray.Direction = bsdf_context.mL;
					continue_bounce = true;
				}
			}

			// Debug - Global
			switch (mConstants.mDebugMode)
			{
			case DebugMode::None:						break;
			case DebugMode::Barycentrics: 				path_context.mEmission = hit_context.mBarycentrics; continue_bounce = false; break;
			case DebugMode::Position: 					path_context.mEmission = hit_context.mHitPositionWS; continue_bounce = false; break;
			case DebugMode::Normal: 					path_context.mEmission = hit_context.mVertexNormalWS; continue_bounce = false; break;
			case DebugMode::UV:							path_context.mEmission = float3(hit_context.mUV, 0.0); continue_bounce = false; break;
			case DebugMode::Albedo: 					path_context.mEmission = BSDFEvaluation::Source::Albedo(hit_context); continue_bounce = false; break;
			case DebugMode::Reflectance: 				path_context.mEmission = InstanceDatas[hit_context.mInstanceID].mReflectance; continue_bounce = false; break;
			case DebugMode::Emission: 					path_context.mEmission = InstanceDatas[hit_context.mInstanceID].mEmission; continue_bounce = false; break;
			case DebugMode::RoughnessAlpha: 			path_context.mEmission = InstanceDatas[hit_context.mInstanceID].mRoughnessAlpha; continue_bounce = false; break;
			case DebugMode::RecursionCount:				continue_bounce = true; break;
			default:									path_context.mEmission = sDebugOutput; continue_bounce = false; break;
			}
		}
		else
		{
			// Background (Miss)

			float3 sky_luminance = GetSkyLuminance();

			float3 cloud_transmittance = 1;
			float3 cloud_luminance = 0;
			RaymarchCloud(cloud_transmittance, cloud_luminance);

			float3 emission = lerp(sky_luminance, cloud_luminance, 1.0 - cloud_transmittance);

			path_context.mEmission = path_context.mEmission + path_context.mThroughput * emission;

			break;
		}

		if (!continue_bounce)
			break;

		// Russian Roulette
		// [TODO] Make a test scene for comparison
		// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting.html
		// https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer
		if (path_context.mRecursionCount >= mConstants.mRecursionCountMax)
		{
			if (mConstants.mRecursionMode == RecursionMode::RussianRoulette && path_context.mRecursionCount <= 16)
			{
				// Probability can be chosen in almost any manner
				// e.g. Fixed threshold
				// e.g. Veach's Efficiency-Optimized Russian roulette is based on average variance and cost
				// Based on throughput here (basically albedo)
				float3 throughput = path_context.mThroughput;
				float termination_probability = max(0.25, 1.0 - max(throughput.x, max(throughput.y, throughput.z)));

				if (RandomFloat01(path_context.mRandomState) < termination_probability)
					break;

				// Weight the path to keep result unbiased
				path_context.mThroughput /= (1 - termination_probability);
			}
			else
				break;
		}

		path_context.mEmission += light_emission; // Keep accumulation from light sample after termination with RR
		path_context.mRecursionCount++;
	}

	// Accumulation
	{
		float3 current_output = path_context.mEmission;

		float3 previous_output = ScreenColorUAV[sGetDispatchRaysIndex().xy].xyz;
		previous_output = max(0, previous_output); // Eliminate nan
		float3 mixed_output = lerp(previous_output, current_output, mConstants.mCurrentFrameWeight);

		if (mConstants.mDebugMode != DebugMode::None)
			mixed_output = current_output;

		if (mConstants.mDebugMode == DebugMode::RecursionCount)
			mixed_output = path_context.mRecursionCount;

		ScreenColorUAV[sGetDispatchRaysIndex().xy] = float4(mixed_output, 1);
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
	ScreenColorUAV.GetDimensions(output_dimensions.x, output_dimensions.y);

	sDispatchRaysIndex.xyz = inDispatchThreadID.xyz;
	sDispatchRaysDimensions = uint3(output_dimensions.xy, 1);

	TraceRay();
}