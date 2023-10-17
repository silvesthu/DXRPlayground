
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "BSDF.h"
#include "Light.h"
#include "Planet.h"
#include "AtmosphereIntegration.h"
#include "CloudIntegration.h"

void TraceRay()
{
	DebugValueInit();

	// From https://www.shadertoy.com/view/tsBBWW
	uint random_state = uint(uint(sGetDispatchRaysIndex().x) * uint(1973) + uint(sGetDispatchRaysIndex().y) * uint(9277) + uint(mConstants.mCurrentFrameIndex) * uint(26699)) | uint(1);

	uint3 launchIndex = sGetDispatchRaysIndex();
	uint3 launchDim = sGetDispatchRaysDimensions();

	float2 crd = float2(launchIndex.xy);
	float2 dims = float2(launchDim.xy);

	switch (mConstants.mOffsetMode)
	{
	case OffsetMode::HalfPixel:	crd += 0.5; break;
	case OffsetMode::Random:	crd += float2(RandomFloat01(random_state), RandomFloat01(random_state)); break;
	case OffsetMode::NoOffset:	// [[fallthrough]];
	default: break;
	}

	float2 d = ((crd / dims) * 2.f - 1.f); // [0,1] => [-1,1]
	d.y = -d.y;
	
	RayDesc ray;
	ray.Origin = mConstants.mCameraPosition.xyz;
	ray.Direction = normalize(mConstants.mCameraDirection.xyz + mConstants.mCameraRightExtend.xyz * d.x + mConstants.mCameraUpExtend.xyz * d.y);
	ray.TMin = 0.001;
	ray.TMax = 100000;

	PathContext path_context					= (PathContext)0;
	path_context.mThroughput					= 1;
	path_context.mPrevBSDFPDF					= 0;
	path_context.mPrevDiracDeltaDistribution	= false;
	path_context.mEtaScale						= 1;
	path_context.mRandomState					= random_state;
	path_context.mRecursionCount				= 0;

	// Note that RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH will give first hit for "Any Hit". The result may not be the closest one
	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	uint additional_ray_flags = 0;
	uint ray_instance_mask = 0xffffffff;

	for (;;)
	{
		path_context.mEmission				+= path_context.mLightEmission;
		path_context.mLightEmission			= 0;

		if (max(path_context.mThroughput.x, max(path_context.mThroughput.y, path_context.mThroughput.z)) <= 0)
			break;

		bool continue_bounce				= false;

		sWorldRayOrigin						= ray.Origin;
		sWorldRayDirection					= ray.Direction;

		query.TraceRayInline(RaytracingScene, additional_ray_flags, ray_instance_mask, ray);
		query.Proceed();

		if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			// System value intrinsics https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#system-value-intrinsics

			sRayTCurrent					= query.CommittedRayT();

			BuiltInTriangleIntersectionAttributes attributes;
			attributes.barycentrics			= query.CommittedTriangleBarycentrics();

			HitContext hit_context			= (HitContext)0;
			hit_context.mInstanceID			= query.CommittedInstanceID();
			hit_context.mPrimitiveIndex		= query.CommittedPrimitiveIndex();
			hit_context.mRayOriginWS		= ray.Origin;
			hit_context.mRayDirectionWS		= ray.Direction;

			if (launchIndex.x == mConstants.mPixelDebugCoord.x && launchIndex.y == mConstants.mPixelDebugCoord.y && path_context.mRecursionCount == 0)
				BufferDebugUAV[0].mPixelInstanceID = hit_context.mInstanceID;

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

				float2 uvs[3] = { UVs[indices[0]], UVs[indices[1]], UVs[indices[2]] };
				hit_context.mUV = uvs[0] * hit_context.mBarycentrics.x + uvs[1] * hit_context.mBarycentrics.y + uvs[2] * hit_context.mBarycentrics.z;
			}

			// Hit position
			{
				hit_context.mHitPositionWS = (sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent());

				// Debug
				DebugValue(PixelDebugMode::PositionWS_InstanceID, path_context.mRecursionCount, float4(hit_context.mHitPositionWS, hit_context.mInstanceID));
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
				if (dot(hit_context.mVertexNormalWS, -sGetWorldRayDirection()) < 0 && !InstanceDatas[hit_context.mInstanceID].mTwoSided)
					emission = 0;

				// IES
				//float angle = acos(dot(-sGetWorldRayDirection(), float3(0,-1,0))) / MATH_PI;
				//float ies = IESSRV.SampleLevel(BilinearClampSampler, float2(angle, 0), 0).x;
				//hit_context.mEmission *= ies;

				if (mConstants.mDebugInstanceIndex == hit_context.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
					emission = hit_context.mBarycentrics;
			}

			// DebugValue(PixelDebugMode::Manual, path_context.mRecursionCount, float4(hit_context.mVertexNormalWS, 0));

			// Lighting
			if (InstanceDatas[hit_context.mInstanceID].mBSDFType == BSDFType::Light)
			{
				// Direct Lighting
				if (path_context.mRecursionCount == 0 || mConstants.mLightCount == 0 || mConstants.mSampleMode == SampleMode::SampleBSDF || path_context.mPrevDiracDeltaDistribution)
				{
					path_context.mEmission += path_context.mThroughput * emission;
				}
				else if (mConstants.mSampleMode == SampleMode::MIS)
				{
					Light light = Lights[InstanceDatas[hit_context.mInstanceID].mLightID];

					float light_sample_pdf = 0;
					float3 light_sample_direction = 0;
					LightEvaluation::GenerateSamplingDirection(light, ray.Origin, path_context, light_sample_direction, light_sample_pdf);

					float light_pdf = light_sample_pdf * LightEvaluation::GetLightSelectionPDF();
					float mis_weight = max(0.0, MIS::PowerHeuristic(1, path_context.mPrevBSDFPDF, 1, light_pdf));
					path_context.mEmission += path_context.mThroughput * emission * mis_weight;

					DebugValue(PixelDebugMode::MIS_BSDF, path_context.mRecursionCount - 1, float4(path_context.mPrevBSDFPDF, light_pdf, LightEvaluation::GetLightSelectionPDF(), light_sample_pdf));
				}
			}
			else
			{
				// DebugValue(PixelDebugMode::Manual, path_context.mRecursionCount, float4(0, 0, 0, 1));

				// Sample Light
				bool sample_light = mConstants.mSampleMode == SampleMode::SampleLight || mConstants.mSampleMode == SampleMode::MIS;
				if (sample_light && mConstants.mLightCount > 0 && !BSDFEvaluation::DiracDeltaDistribution(hit_context))
				{
					uint light_index = min(RandomFloat01(path_context.mRandomState) * mConstants.mLightCount, mConstants.mLightCount - 1);
					Light light = Lights[light_index];

					float light_sample_pdf = 0;
					float3 light_sample_direction = 0;
					LightEvaluation::GenerateSamplingDirection(light, hit_context.mHitPositionWS, path_context, light_sample_direction, light_sample_pdf);

					// DebugValue(PixelDebugMode::Manual, path_context.mRecursionCount, float4(0, 0, 0, 1));

					float light_pdf = light_sample_pdf * LightEvaluation::GetLightSelectionPDF();
					if (light_pdf > 0)
					{
						RayDesc shadow_ray;
						shadow_ray.Origin = hit_context.mHitPositionWS;
						shadow_ray.Direction = normalize(light_sample_direction);
						shadow_ray.TMin = 0.001;
						shadow_ray.TMax = 1E6; // long distance

						RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> shadow_query;
						uint additional_shadow_ray_flags = 0;
						uint shadow_ray_instance_mask = 0xffffffff;

						shadow_query.TraceRayInline(RaytracingScene, additional_shadow_ray_flags, shadow_ray_instance_mask, shadow_ray);
						shadow_query.Proceed();

						// DebugValue(PixelDebugMode::Manual, path_context.mRecursionCount, float4(shadow_query.CommittedInstanceID(), light.mInstanceID, 0, 1));

						if (shadow_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT && shadow_query.CommittedInstanceID() == light.mInstanceID)
						{
							BSDFContext bsdf_context = BSDFEvaluation::GenerateContext(BSDFContext::Mode::LightSample, shadow_ray.Direction, hit_context.mVertexNormalWS, -hit_context.mRayDirectionWS, 1.0, hit_context);
							BSDFEvaluation::Evaluate(hit_context, bsdf_context, path_context);

							float3 luminance = light.mEmission * (kEmissionBoostScale * kPreExposure);
							float3 emission = luminance * bsdf_context.mBSDF * abs(bsdf_context.mNdotL) / light_pdf;

							if (mConstants.mSampleMode == SampleMode::MIS)
								emission *= max(0.0, MIS::PowerHeuristic(1, light_pdf, 1, bsdf_context.mBSDFPDF));

							DebugValue(PixelDebugMode::MIS_Light, path_context.mRecursionCount, float4(bsdf_context.mBSDFPDF, light_pdf, LightEvaluation::GetLightSelectionPDF(), light_sample_pdf));

							path_context.mLightEmission = path_context.mThroughput * emission;

							// DebugValue(PixelDebugMode::Manual, path_context.mRecursionCount, float4(bsdf_context.mBSDF, light_pdf));
							// DebugValue(PixelDebugMode::Manual, path_context.mRecursionCount, float4(emission, 1));
							// DebugTexture(path_context.mRecursionCount == 0, float4(bsdf_context.mBSDF, 0));
						}
					}
				}

				// Sample BSDF
				{
					// Generate next sample based on BSDF
					BSDFContext bsdf_context = BSDFEvaluation::GenerateImportanceSamplingContext(hit_context.mVertexNormalWS, -hit_context.mRayDirectionWS, hit_context, path_context);
					BSDFEvaluation::Evaluate(hit_context, bsdf_context, path_context);

					// DebugValue(PixelDebugMode::Manual, path_context.mRecursionCount, float4(hit_context.mVertexNormalWS, 0));
					// DebugValue(PixelDebugMode::Manual, path_context.mRecursionCount, float4(bsdf_context.mL, 0));
					DebugTexture(path_context.mRecursionCount == 0, float4(bsdf_context.mBSDF, 0));

					path_context.mEmission += path_context.mThroughput * emission; // Non-light emission
					path_context.mThroughput *= bsdf_context.mBSDFPDF > 0 ? (bsdf_context.mBSDF * abs(bsdf_context.mNdotL) / bsdf_context.mBSDFPDF) : 0;
					path_context.mPrevBSDFPDF = bsdf_context.mBSDFPDF;
					path_context.mPrevDiracDeltaDistribution = BSDFEvaluation::DiracDeltaDistribution(hit_context);
					path_context.mEtaScale *= bsdf_context.mEta;

					DebugValue(PixelDebugMode::BSDF_PDF, path_context.mRecursionCount, float4(bsdf_context.mBSDF, bsdf_context.mBSDFPDF));
					DebugValue(PixelDebugMode::Throughput_DiracDelta, path_context.mRecursionCount, float4(path_context.mThroughput, path_context.mPrevDiracDeltaDistribution));

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
			case DebugMode::Reflectance: 				path_context.mEmission = InstanceDatas[hit_context.mInstanceID].mSpecularReflectance; continue_bounce = false; break;
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

		float throughput_max = max(path_context.mThroughput.x, max(path_context.mThroughput.y, path_context.mThroughput.z));

		// Russian Roulette
		// [TODO] Make a test scene for comparison
		// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting.html
		// https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer
		if (path_context.mRecursionCount >= mConstants.mRecursionCountMax)
		{
			if (mConstants.mRecursionMode == RecursionMode::FixedCount)
				continue_bounce = false;

			if (mConstants.mRecursionMode == RecursionMode::RussianRoulette)
			{
				// Probability can be chosen in almost any manner
				// e.g. Fixed threshold
				// e.g. Veach's Efficiency-Optimized Russian roulette is based on average variance and cost
				float scale = path_context.mEtaScale * path_context.mEtaScale; // See Dielectric::Evaluate
				float continue_probablity = min(throughput_max * scale, 0.95);
				bool continue_by_russian_roulette = RandomFloat01(path_context.mRandomState) < continue_probablity;

				DebugValue(PixelDebugMode::RussianRoulette_Probability_EtaScale, path_context.mRecursionCount, float4(continue_by_russian_roulette, continue_probablity, path_context.mEtaScale, 0));

				if (continue_by_russian_roulette)
				{
					// Weight the path to keep result unbiased
					path_context.mThroughput /= continue_probablity;
				}
				else
				{
					// Termination by Russian Roulette
					continue_bounce = false;
				}
			}
		}

		if (continue_bounce)
		{
			path_context.mRecursionCount++;
		}
		else
		{
			break;
		}
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