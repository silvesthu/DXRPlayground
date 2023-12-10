
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "BSDF.h"
#include "Light.h"
#include "Planet.h"
#include "AtmosphereIntegration.h"
#include "CloudIntegration.h"

void TraceRay(PixelContext inPixelContext)
{
	DebugValueInit();

	// From https://www.shadertoy.com/view/tsBBWW
	// [TODO] Need proper noise
	uint random_state							= uint(uint(inPixelContext.mPixelIndex.x) * uint(1973) + uint(inPixelContext.mPixelIndex.y) * uint(9277) + uint(mConstants.mCurrentFrameIndex) * uint(26699)) | uint(1);

	float2 screen_coords						= float2(inPixelContext.mPixelIndex.xy);
	float2 screen_size							= float2(inPixelContext.mPixelTotal.xy);

	switch (mConstants.mOffsetMode)
	{
	case OffsetMode::HalfPixel:	screen_coords	+= 0.5; break;
	case OffsetMode::Random:	screen_coords	+= float2(RandomFloat01(random_state), RandomFloat01(random_state)); break;
	case OffsetMode::NoOffset:					// [[fallthrough]];
	default: break;
	}

	float2 ndc_xy								= ((screen_coords / screen_size) * 2.f - 1.f);	// [0,1] => [-1,1]
	ndc_xy.y									= -ndc_xy.y;									// Flip y
	
	RayDesc ray;
	ray.Origin									= mConstants.mCameraPosition.xyz;
	ray.Direction								= normalize(mConstants.mCameraDirection.xyz + mConstants.mCameraRightExtend.xyz * ndc_xy.x + mConstants.mCameraUpExtend.xyz * ndc_xy.y);
	ray.TMin									= 0.001;
	ray.TMax									= 100000;

	PathContext path_context					= (PathContext)0;
	path_context.mThroughput					= 1.0;
	path_context.mPrevBSDFSamplePDF				= 0.0;
	path_context.mPrevDiracDeltaDistribution	= false;
	path_context.mEtaScale						= 1.0;
	path_context.mRandomState					= random_state;
	path_context.mRecursionCount				= 0;

	for (;;)
	{
		// Defer the accumulation from light sample to make recursion count easier
		path_context.mEmission					+= path_context.mLightEmission;
		path_context.mLightEmission				= 0;

		// Drop the ray if throughput is 0
		if (max(path_context.mThroughput.x, max(path_context.mThroughput.y, path_context.mThroughput.z)) <= 0)
			break;

		bool continue_bounce					= false;

		// Helper
		sWorldRayOrigin							= ray.Origin;
		sWorldRayDirection						= ray.Direction;

		// Note that RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH will give first hit for "Any Hit". The result may not be the closest one
		// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
		RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
		uint additional_ray_flags				= 0;
		uint ray_instance_mask					= 0xffffffff;
		query.TraceRayInline(RaytracingScene, additional_ray_flags, ray_instance_mask, ray);
		query.Proceed();

		if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			// Ray hit something
			
			// System value intrinsics https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#system-value-intrinsics

			// Helper
			sRayTCurrent						= query.CommittedRayT();

			BuiltInTriangleIntersectionAttributes attributes;
			attributes.barycentrics				= query.CommittedTriangleBarycentrics();

			HitContext hit_context				= (HitContext)0;
			hit_context.mInstanceID				= query.CommittedInstanceID();
			hit_context.mPrimitiveIndex			= query.CommittedPrimitiveIndex();
			hit_context.mRayOriginWS			= ray.Origin;
			hit_context.mRayDirectionWS			= ray.Direction;
			hit_context.mRayTCurrent			= query.CommittedRayT();
			hit_context.mBarycentrics			= float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);
			
			// Vertex attributes
			{
				// Only support 32bit index for simplicity
				// see https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl for reference
				uint kIndexCountPerTriangle		= 3;
				uint base_index					= hit_context.mPrimitiveIndex * kIndexCountPerTriangle + InstanceDatas[hit_context.mInstanceID].mIndexOffset;
				uint3 indices					= uint3(Indices[base_index], Indices[base_index + 1], Indices[base_index + 2]);

				float3 vertices[3]				= { Vertices[indices[0]], Vertices[indices[1]], Vertices[indices[2]] };
				hit_context.mVertexPositionOS	= vertices[0] * hit_context.mBarycentrics.x + vertices[1] * hit_context.mBarycentrics.y + vertices[2] * hit_context.mBarycentrics.z;

				float3 normals[3]				= { Normals[indices[0]], Normals[indices[1]], Normals[indices[2]] };
				float3 normal					= normalize(normals[0] * hit_context.mBarycentrics.x + normals[1] * hit_context.mBarycentrics.y + normals[2] * hit_context.mBarycentrics.z);
				hit_context.mVertexNormalOS		= normal;
				hit_context.mVertexNormalWS		= normalize(mul((float3x3) InstanceDatas[hit_context.mInstanceID].mInverseTranspose, normal)); // Allow non-uniform scale

				float2 uvs[3]					= { UVs[indices[0]], UVs[indices[1]], UVs[indices[2]] };
				hit_context.mUV					= uvs[0] * hit_context.mBarycentrics.x + uvs[1] * hit_context.mBarycentrics.y + uvs[2] * hit_context.mBarycentrics.z;
			}
			
			// HitConext is constructed, fill the debug values
			{
				// Report InstanceID of instance at PixelDebugCoord
				if (inPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && inPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y && path_context.mRecursionCount == 0)
					BufferDebugUAV[0].mPixelInstanceID			= hit_context.mInstanceID;
				
				DebugValue(PixelDebugMode::PositionWS, path_context.mRecursionCount, float3(hit_context.PositionWS()));
				DebugValue(PixelDebugMode::InstanceID, path_context.mRecursionCount, float3(hit_context.mInstanceID, 0.0, 0.0));
			}

			// Participating media
			{
				float3 in_scattering			= 0;
				float3 transmittance			= 1;
				GetSkyLuminanceToPoint(in_scattering, transmittance);

				path_context.mEmission			+= in_scattering;
				path_context.mThroughput		*= transmittance;

				DebugModeValue(DebugMode::InScattering, in_scattering);
				DebugModeValue(DebugMode::Transmittance, transmittance);
			}

			// Emission
			float3 emission = hit_context.Emission() * (kEmissionBoostScale * kPreExposure);
			{
				if (dot(hit_context.mVertexNormalWS, -hit_context.mRayDirectionWS) < 0 && !hit_context.TwoSided())
					emission = 0;

				// IES
				//float angle = acos(dot(-sGetWorldRayDirection(), float3(0,-1,0))) / MATH_PI;
				//float ies = IESSRV.SampleLevel(BilinearClampSampler, float2(angle, 0), 0).x;
				//hit_context.mEmission *= ies;

				if (mConstants.mDebugInstanceIndex == hit_context.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
					emission = hit_context.mBarycentrics;
			}
			
			// Ray hit a light
			if (hit_context.BSDF() == BSDF::Light)
			{
				if (path_context.mRecursionCount == 0 ||					// Camera ray hit the light
					mConstants.mLightCount == 0 ||							// No light -> no light sample
					path_context.mPrevDiracDeltaDistribution || 			// Prev hit is DiracDeltaDistribution -> no light sample
					mConstants.mSampleMode == SampleMode::SampleBSDF ||		// SampleBSDF mode -> no light sample
					false
					)
				{
					// Add light contribution
					
					path_context.mEmission						+= path_context.mThroughput * emission;
				}
				else if (mConstants.mSampleMode == SampleMode::MIS)
				{
					// Add light contribution with MIS
					
					Light light = Lights[hit_context.LightIndex()];

					float3 light_sample_direction				= 0;
					float light_sample_direction_pdf			= 0;
					LightEvaluation::GenerateSamplingDirection(light, ray.Origin, path_context, light_sample_direction, light_sample_direction_pdf);

					float light_pdf								= light_sample_direction_pdf * LightEvaluation::SelectLightPDF(hit_context.LightIndex());
					float mis_weight							= max(0.0, MIS::PowerHeuristic(1, path_context.mPrevBSDFSamplePDF, 1, light_pdf));
					path_context.mEmission						+= path_context.mThroughput * emission * mis_weight;

					DebugValue(PixelDebugMode::BSDF__MIS, path_context.mRecursionCount - 1 /* for prev BSDF hit */, float3(path_context.mPrevBSDFSamplePDF, light_pdf, mis_weight));
				}
			}
			else
			{
				// Sample light (NEE)
				bool sample_light = mConstants.mSampleMode == SampleMode::SampleLight || mConstants.mSampleMode == SampleMode::MIS;
				if (mConstants.mLightCount > 0 &&							// No light -> no light sample
					!hit_context.DiracDeltaDistribution() &&				// Current hit is DiracDeltaDistribution -> no light sample
					sample_light &&											// SampleBSDF mode -> no light sample
					true
					)
				{
					// Select light
					uint light_index							= LightEvaluation::SelectLight(path_context);
					Light light									= Lights[light_index];

					float light_sample_direction_pdf			= 0;
					float3 light_sample_direction				= 0;
					LightEvaluation::GenerateSamplingDirection(light, hit_context.PositionWS(), path_context, light_sample_direction, light_sample_direction_pdf);
					
					float light_pdf								= light_sample_direction_pdf * LightEvaluation::SelectLightPDF(light_index);
					if (light_pdf > 0)
					{
						// Cast shadow ray
						RayDesc shadow_ray;
						shadow_ray.Origin						= hit_context.PositionWS();
						shadow_ray.Direction					= normalize(light_sample_direction);
						shadow_ray.TMin							= 0.001;
						shadow_ray.TMax							= 100000;

						RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> shadow_query;
						uint shadow_additional_ray_flags		= 0;
						uint shadow_ray_instance_mask			= 0xffffffff;
						shadow_query.TraceRayInline(RaytracingScene, shadow_additional_ray_flags, shadow_ray_instance_mask, shadow_ray);
						shadow_query.Proceed();
						
						// Shadow ray hit the light
						if (shadow_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT && shadow_query.CommittedInstanceID() == light.mInstanceID)
						{
							BSDFContext bsdf_context			= BSDFContext::Generate(BSDFContext::Mode::LightSample, shadow_ray.Direction, hit_context.mVertexNormalWS, -hit_context.mRayDirectionWS, 1.0, hit_context);
							BSDFResult bsdf_result				= BSDFEvaluation::Evaluate(bsdf_context, hit_context, path_context);
							
							DebugValue(PixelDebugMode::Light_BSDF,	path_context.mRecursionCount, float3(bsdf_result.mBSDF));
							DebugValue(PixelDebugMode::Light_PDF,	path_context.mRecursionCount, float3(light_pdf, 0, 0));

							float3 luminance					= light.mEmission * (kEmissionBoostScale * kPreExposure);
							float3 light_emission				= luminance * bsdf_result.mBSDF * abs(bsdf_context.mNdotL) / light_pdf;

							if (mConstants.mSampleMode == SampleMode::MIS)
							{
								float mis_weight				= max(0.0, MIS::PowerHeuristic(1, light_pdf, 1, bsdf_result.mBSDFSamplePDF));
								light_emission					*= mis_weight;
								
								DebugValue(PixelDebugMode::Light_MIS,	path_context.mRecursionCount, float3(bsdf_result.mBSDFSamplePDF, light_pdf, mis_weight));
							}

							path_context.mLightEmission			= path_context.mThroughput * light_emission;
						}
					}
				}

				// Sample BSDF
				{
					BSDFContext bsdf_context					= BSDFEvaluation::GenerateImportanceSamplingContext(hit_context, path_context);
					BSDFResult bsdf_result						= BSDFEvaluation::Evaluate(bsdf_context, hit_context, path_context);
					
					path_context.mEmission						+= path_context.mThroughput * emission; // Emissive BSDF
					path_context.mThroughput					*= bsdf_result.mBSDFSamplePDF > 0 ? (bsdf_result.mBSDF * abs(bsdf_context.mNdotL) / bsdf_result.mBSDFSamplePDF) : 0;
					path_context.mEtaScale						*= bsdf_result.mEta;
					
					path_context.mPrevBSDFSamplePDF				= bsdf_result.mBSDFSamplePDF;
					path_context.mPrevDiracDeltaDistribution	= hit_context.DiracDeltaDistribution();

					DebugValue(PixelDebugMode::BSDF__BSDF,		path_context.mRecursionCount, float3(bsdf_result.mBSDF));
					DebugValue(PixelDebugMode::BSDF__PDF,		path_context.mRecursionCount, float3(bsdf_result.mBSDFSamplePDF, 0, 0));
					DebugValue(PixelDebugMode::Throughput,		path_context.mRecursionCount, float3(path_context.mThroughput));
					DebugValue(PixelDebugMode::DiracDelta,		path_context.mRecursionCount, float3(path_context.mPrevDiracDeltaDistribution, 0, 0));

					// Prepare for next bounce
					ray.Origin									= hit_context.PositionWS();
					ray.Direction								= bsdf_context.mL;
					continue_bounce								= true;
				}
			}

			// DebugMode
			switch (mConstants.mDebugMode)
			{
			case DebugMode::None:				break;
			case DebugMode::Barycentrics: 		path_context.mEmission = hit_context.mBarycentrics; continue_bounce = false; break;
			case DebugMode::Position: 			path_context.mEmission = hit_context.PositionWS(); continue_bounce = false; break;
			case DebugMode::Normal: 			path_context.mEmission = hit_context.mVertexNormalWS; continue_bounce = false; break;
			case DebugMode::UV:					path_context.mEmission = float3(hit_context.mUV, 0.0); continue_bounce = false; break;
			case DebugMode::Albedo: 			path_context.mEmission = hit_context.Albedo(); continue_bounce = false; break;
			case DebugMode::Reflectance: 		path_context.mEmission = InstanceDatas[hit_context.mInstanceID].mSpecularReflectance; continue_bounce = false; break;
			case DebugMode::Emission: 			path_context.mEmission = InstanceDatas[hit_context.mInstanceID].mEmission; continue_bounce = false; break;
			case DebugMode::RoughnessAlpha: 	path_context.mEmission = InstanceDatas[hit_context.mInstanceID].mRoughnessAlpha; continue_bounce = false; break;
			case DebugMode::RecursionCount:		continue_bounce = true; break;
			default:							path_context.mEmission = sDebugModeValue; continue_bounce = false; break;
			}
		}
		else
		{
			// Ray missed (Background)

			float3 sky_luminance				= GetSkyLuminance();

			float3 cloud_transmittance			= 1;
			float3 cloud_luminance				= 0;
			RaymarchCloud(cloud_transmittance, cloud_luminance);

			float3 emission						= lerp(sky_luminance, cloud_luminance, 1.0 - cloud_transmittance);
			path_context.mEmission				= path_context.mEmission + path_context.mThroughput * emission;

			break;
		}

		float throughput_max					= max(path_context.mThroughput.x, max(path_context.mThroughput.y, path_context.mThroughput.z));

		// Russian Roulette
		// [TODO] Make a test scene for comparison
		// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting.html
		// https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer
		if (path_context.mRecursionCount >= mConstants.mRecursionCountMax)
		{
			if (mConstants.mRecursionMode == RecursionMode::FixedCount)
				continue_bounce					= false;

			if (mConstants.mRecursionMode == RecursionMode::RussianRoulette)
			{
				// Probability can be chosen in almost any manner
				// e.g. Fixed threshold
				// e.g. Veach's Efficiency-Optimized Russian roulette is based on average variance and cost
				float scale						= path_context.mEtaScale * path_context.mEtaScale; // See Dielectric::Evaluate
				float continue_probablity		= min(throughput_max * scale, 0.95);
				float probablity				= RandomFloat01(path_context.mRandomState);
				bool probability_passed			= probablity < continue_probablity;

				DebugValue(PixelDebugMode::RussianRoulette,		path_context.mRecursionCount, float3(probability_passed, probablity, continue_probablity));
				DebugValue(PixelDebugMode::EtaScale,			path_context.mRecursionCount, float3(path_context.mEtaScale, 0, 0));				

				if (probability_passed)
				{
					// Weight the path to keep result unbiased
					path_context.mThroughput	/= continue_probablity;
				}
				else
				{
					// Termination by Russian Roulette
					continue_bounce = false;
				}
			}
		}

		if (continue_bounce)
			path_context.mRecursionCount++;
		else
			break;
	}

	// Accumulation
	{
		float3 current_output					= path_context.mEmission;
		float3 previous_output					= ScreenColorUAV[inPixelContext.mPixelIndex.xy].xyz;
		previous_output							= max(0, previous_output); // Eliminate nan
		float3 mixed_output						= lerp(previous_output, current_output, mConstants.mCurrentFrameWeight);

		if (mConstants.mDebugMode != DebugMode::None)
			mixed_output						= current_output;

		if (mConstants.mDebugMode == DebugMode::RecursionCount)
			mixed_output						= path_context.mRecursionCount;

		ScreenColorUAV[inPixelContext.mPixelIndex.xy] = float4(mixed_output, 1);
		ScreenDebugUAV[inPixelContext.mPixelIndex.xy] = sDebugValue;
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

	// Helper
	sDispatchRaysIndex.xyz						= inDispatchThreadID.xyz;
	sDispatchRaysDimensions						= uint3(output_dimensions.xy, 1);
	
	PixelContext pixel_context;
	pixel_context.mPixelIndex					= inDispatchThreadID.xyz;
	pixel_context.mPixelTotal					= uint3(output_dimensions.xy, 1);
	TraceRay(pixel_context);
}