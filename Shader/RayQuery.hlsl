
#include "Shared.h"
#include "HLSL.h"
#include "Binding.h"
#include "Common.h"
#include "Context.h"
#include "BSDF.h"
#include "Light.h"
#include "AtmosphereIntegration.h"
#include "CloudIntegration.h"

#ifdef NVAPI_LSS
#define IsHit(query) (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT || NvRtCommittedIsLss(query) || NvRtCommittedIsSphere(query))
#else
#define IsHit(query) (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
#endif // NVAPI_LSS

void TraceRay(inout PixelContext ioPixelContext)
{
	DebugValueInit();
	
	// From https://www.shadertoy.com/view/tsBBWW
	// [TODO] Need proper noise
	uint random_state							= uint(uint(ioPixelContext.mPixelIndex.x) * uint(1973) + uint(ioPixelContext.mPixelIndex.y) * uint(9277) + uint(mConstants.mCurrentFrameIndex) * uint(26699)) | uint(1);
	uint random_state_restir					= uint(uint(ioPixelContext.mPixelIndex.x) * uint(1973) + uint(ioPixelContext.mPixelIndex.y) * uint(9277) + uint(mConstants.mReSTIR.mTemporalCounter) * uint(26699)) | uint(1);

	float2 screen_coords						= float2(ioPixelContext.mPixelIndex.xy);
	float2 screen_size							= float2(ioPixelContext.mPixelTotal.xy);

	// [TODO] Need proper reconstruction filter, see https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Image_Reconstruction
	switch (mConstants.mOffsetMode)
	{
	case OffsetMode::HalfPixel:	screen_coords	+= 0.5; break;
	case OffsetMode::Random:	screen_coords	+= float2(RandomFloat01(random_state), RandomFloat01(random_state)); break;
	case OffsetMode::NoOffset:					// [[fallthrough]];
	default: break;
	}

	float2 ndc_xy								= ((screen_coords / screen_size) * 2.f - 1.f);							// [0,1] => [-1,1]
	ndc_xy.y									= -ndc_xy.y;															// Flip y
	float4 point_on_near_plane					= mul(mConstants.mInverseProjectionMatrix, float4(ndc_xy, 0.0, 1.0));
	float3 ray_direction_vs						= normalize(point_on_near_plane.xyz / point_on_near_plane.w);
	float3 ray_direction_ws						= mul(mConstants.mInverseViewMatrix, float4(ray_direction_vs, 0.0)).xyz;

	// [TODO] Use btter offset on Origin and TMin
	RayDesc ray;
	ray.Origin									= mConstants.CameraPosition().xyz;
	ray.Direction								= ray_direction_ws;
	ray.TMin									= 1E-4;
	ray.TMax									= 100000;

	PathContext path_context					= (PathContext)0;
	path_context.mThroughput					= 1.0;
	path_context.mPrevBSDFSamplePDF				= 0.0;
	path_context.mPrevDiracDeltaDistribution	= false;
	path_context.mEtaScale						= 1.0;
	path_context.mRandomState					= random_state;
	path_context.mRandomStateReSTIR				= random_state_restir;
	path_context.mRecursionDepth				= 0;
	path_context.mMediumInstanceID				= InvalidInstanceID;

	if (mConstants.mDebugFlag & DebugFlag::UpdateRayInspection)
		if (ioPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && ioPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y)
			RayInspectionUAV[0].mPositionWS[0] = float4(ray.Origin + ray.Direction * ray.TMin, 1.0);

	for (;;)
	{
		bool continue_bounce					= false;

		// Note RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH will give first hit but not necessary the closest one, commonly used for shadow ray
		// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
		RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
		uint additional_ray_flags				= 0;
		uint ray_instance_mask					= 0xffffffff;
		query.TraceRayInline(RaytracingScene, additional_ray_flags, ray_instance_mask, ray);
		query.Proceed();
		
		if (ioPixelContext.mOutputDepth)
		{
			if (IsHit(query))
			{
				float4 position_ws				= float4(ray.Origin + ray.Direction * query.CommittedRayT(), 1.0);
				float4 position_ps				= mul(mConstants.mViewProjectionMatrix, position_ws);
				float4 position_ndc				= position_ps.xyzw / position_ps.w;
				
				ioPixelContext.mDepth			= position_ndc.z;
				// DebugValue(position_ndc.xyz);
			}
			else
			{
				ioPixelContext.mDepth			= 1.0;
				// DebugValue(0);
			}
			return;
		}

		if (path_context.mRecursionDepth == 0)
		{
			// DebugValue(query.CommittedStatus());
		}
		
		if (IsHit(query))
		{
			// Ray hit something

			// HitContext
			HitContext hit_context				= HitContext::Generate(ray, query);

			// Participating media (Medium)
			// [TODO] Need a medium stack to handle nested medium
			// [TODO] Skip medium in case ray is offseted to outside, or mesh is not water tight
			MediumContext medium_context		= MediumContext::Generate(ray, query);
			if (path_context.mMediumInstanceID != InvalidInstanceID && medium_context.mInstanceID == path_context.mMediumInstanceID)
			{
				float free_flight_distance		= -log(1.0 - RandomFloat01(path_context.mRandomState)) / MaxComponent(medium_context.SigmaT());

				if (free_flight_distance < hit_context.mRayWS.mTCurrent)
				{
					medium_context.mRayWS.mTCurrent = free_flight_distance;
					medium_context.mScatteringEvent = true;

					path_context.mThroughput	*= medium_context.Albedo();

					// Prepare for next bounce
					ray.Origin					= ray.Origin + ray.Direction * free_flight_distance;
					ray.Direction				= RandomUnitVector(path_context.mRandomState);
					continue_bounce				= true;
				}

				DebugValue(DebugMode::MediumFreeFlight,			path_context.mRecursionDepth, float3(free_flight_distance, hit_context.mRayWS.mTCurrent, free_flight_distance < hit_context.mRayWS.mTCurrent ? 1 : 0));
			}
			{
				DebugValue(DebugMode::MediumInstanceID,			path_context.mRecursionDepth, float3(path_context.mMediumInstanceID, 0, 0));
			}

			// Debug
			{
				if (mConstants.mDebugFlag & DebugFlag::UpdateRayInspection)
					if (ioPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && ioPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y)
						RayInspectionUAV[0].mPositionWS[path_context.mRecursionDepth + 1] = float4(medium_context.PositionWS(), 1.0);

				if (path_context.mRecursionDepth == 0)
					if (ioPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && ioPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y)
						PixelInspectionUAV[0].mPixelInstanceID = medium_context.mInstanceID;

				DebugValue(DebugMode::PositionWS, path_context.mRecursionDepth, float3(medium_context.PositionWS()));
				DebugValue(DebugMode::DirectionWS, path_context.mRecursionDepth, float3(medium_context.DirectionWS()));
				DebugValue(DebugMode::InstanceID, path_context.mRecursionDepth, float3(medium_context.mInstanceID, 0.0, 0.0));

				//Texture3D<float> ErosionNoise3D = ResourceDescriptorHeap[(int)ViewDescriptorIndex::ErosionNoise3DSRV];
				//float3 offset = float3(0, -mConstants.mSequenceFrameRatio * 5.0, 0);
				//float noise_value = ErosionNoise3D.SampleLevel(BilinearWrapSampler, (hit_context.PositionWS() + offset) * 1.0, 0);
				//noise_value = saturate(pow(noise_value * 1.2, 4.0));
				//DebugValue(DebugMode::Manual, path_context.mRecursionDepth, noise_value);
			}

			// Participating media (Atmosphere)
			{
				float3 in_scattering			= 0;
				float3 transmittance			= 1;

				GetSkyLuminanceToPoint(hit_context.mRayWS, in_scattering, transmittance);

				path_context.mEmission			+= in_scattering;
				path_context.mThroughput		*= transmittance;

				VisualizeValue(VisualizeMode::InScattering, in_scattering);
				VisualizeValue(VisualizeMode::Transmittance, transmittance);
			}

			// Emission
			float3 emission = hit_context.Emission() * (mConstants.mEmissionBoost * kPreExposure);
			{
				if (dot(hit_context.mVertexNormalWS, hit_context.ViewWS()) < 0 && !hit_context.TwoSided())
					emission = 0;

				// IES
				//float angle = acos(dot(-hit_context.mRayDirectionWS, float3(0,-1,0))) / MATH_PI;
				//float ies = IESSRV.SampleLevel(BilinearClampSampler, float2(angle, 0), 0).x;
				//hit_context.mEmission *= ies;

				if (mConstants.mDebugInstanceIndex == hit_context.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
					emission = hit_context.mBarycentrics;
			}
			
			// Ray hit a light / [Mitsuba] Direct emission
			if (medium_context.mScatteringEvent)
			{
				// To next bounce, prepared above
			}
			else if (hit_context.BSDF() == BSDF::Light)
			{
				if (path_context.mRecursionDepth == 0 ||					// Camera ray hit the light
					path_context.mPrevDiracDeltaDistribution || 			// Prev hit is DiracDeltaDistribution -> no light sample
					mConstants.mSampleMode == SampleMode::SampleBSDF ||		// SampleBSDF mode -> no light sample
					false)
				{
					// Add light contribution
					
					path_context.mEmission						+= path_context.mThroughput * emission;
					
					if (path_context.mRecursionDepth == 0)
						DebugValue(DebugMode::LightIndex, path_context.mRecursionDepth, hit_context.LightIndex() + 0.5); // Add a offset to identify light source in LightIndex debug output
				}
				else if (mConstants.mSampleMode == SampleMode::MIS)
				{
					// Add light contribution with MIS
					
					// Select light
					uint light_index							= hit_context.LightIndex();
					Light light									= Lights[light_index];

					LightContext light_context					= LightEvaluation::GenerateContext(LightEvaluation::ContextType::Input, ray.Direction, light_index, ray.Origin, path_context);
					float light_mis_pdf							= light_context.mSolidAnglePDF * light_context.UniformSelectionPDF();
					
					float mis_weight							= max(0.0f, MIS::PowerHeuristic(1, path_context.mPrevBSDFSamplePDF, 1, light_mis_pdf));
					path_context.mEmission						+= path_context.mThroughput * emission * mis_weight;
					
					DebugValue(DebugMode::MIS_BSDF,				path_context.mRecursionDepth - 1 /* for prev BSDF hit */, float3(path_context.mPrevBSDFSamplePDF, light_mis_pdf, mis_weight));
				}
			}
			// Ray hit a surface
			else
			{
				// Sample light (NEE) / [Mitsuba] Emitter sampling
				bool sample_light = mConstants.mSampleMode == SampleMode::SampleLight || mConstants.mSampleMode == SampleMode::MIS;
				if (mConstants.mLightCount > 0 &&							// No light -> no light sample
					!hit_context.DiracDeltaDistribution() &&				// Current hit is DiracDeltaDistribution -> no light sample
					sample_light &&											// SampleBSDF mode -> no light sample
					true)
				{
					// Select light
					LightContext light_context					= LightEvaluation::SelectLight(hit_context.PositionWS(), path_context);
					DebugValue(DebugMode::LightIndex, path_context.mRecursionDepth, light_context.LightIndex());
					
					float light_uniform_pdf						= light_context.mSolidAnglePDF * light_context.UniformSelectionPDF(); // [TODO] Unify MIS (with BRDF sample) to use same mis weight
					float light_weight							= light_context.mSolidAnglePDF <= 0.0 ? 0.0 : (light_context.SelectionWeight() / light_context.mSolidAnglePDF);
					// light_weight								= 1.0 / light_uniform_pdf; // for uniform sample debugging

					DebugValue(DebugMode::RIS_SAMPLE,	path_context.mRecursionDepth, float3(light_context.mReservoir.mTargetPDF, 0.0, 0.0));
					DebugValue(DebugMode::RIS_SUM,		path_context.mRecursionDepth, float3(light_context.mReservoir.mWeightSum, light_context.mReservoir.mCountSum, 0.0));
					
					if (light_context.IsValid() && light_weight > 0)
					{
						// Cast shadow ray
						RayDesc shadow_ray;
						shadow_ray.Origin						= hit_context.PositionWS();
						shadow_ray.Direction					= light_context.mL;
						shadow_ray.TMin							= 0.001;
						shadow_ray.TMax							= 100000;

						RayQuery<RAY_FLAG_FORCE_OPAQUE> shadow_query;
						uint shadow_additional_ray_flags		= 0;
						uint shadow_ray_instance_mask			= 0xffffffff;
						shadow_query.TraceRayInline(RaytracingScene, shadow_additional_ray_flags, shadow_ray_instance_mask, shadow_ray);
						shadow_query.Proceed();
						
						// Shadow ray hit the light
						if (IsHit(shadow_query) && shadow_query.CommittedInstanceID() == light_context.Light().mInstanceID)
						{
							if (mConstants.mDebugFlag& DebugFlag::UpdateRayInspection)
								if (ioPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && ioPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y)
									RayInspectionUAV[0].mLightPositionWS[path_context.mRecursionDepth + 1] = float4(shadow_ray.Origin + shadow_ray.Direction * shadow_query.CommittedRayT(), 1.0);
							
							BSDFContext bsdf_context			= BSDFContext::Generate(BSDFContext::Mode::Light, light_context.mL, hit_context);
							BSDFResult bsdf_result				= BSDFEvaluation::Evaluate(bsdf_context, hit_context, path_context);
							
							DebugValue(DebugMode::Light_L, 		path_context.mRecursionDepth, float3(bsdf_context.mL));
							DebugValue(DebugMode::Light_V, 		path_context.mRecursionDepth, float3(bsdf_context.mV));
							DebugValue(DebugMode::Light_N, 		path_context.mRecursionDepth, float3(bsdf_context.mN));
							DebugValue(DebugMode::Light_H, 		path_context.mRecursionDepth, float3(bsdf_context.mH));

							DebugValue(DebugMode::Light_BSDF,	path_context.mRecursionDepth, float3(bsdf_result.mBSDF));
							DebugValue(DebugMode::Light_PDF,	path_context.mRecursionDepth, float3(1.0 / light_weight, 0, 0));

							float3 luminance					= light_context.Light().mEmission * (mConstants.mEmissionBoost * kPreExposure);
							float3 light_emission				= luminance * bsdf_result.mBSDF * abs(bsdf_context.mNdotL) * light_weight;

							if (mConstants.mSampleMode == SampleMode::MIS)
							{
								float mis_weight				= max(0.0f, MIS::PowerHeuristic(1, light_uniform_pdf, 1, bsdf_result.mBSDFSamplePDF));
								light_emission					*= mis_weight;
								
								DebugValue(DebugMode::MIS_LIGHT, path_context.mRecursionDepth, float3(bsdf_result.mBSDFSamplePDF, light_uniform_pdf, mis_weight));
							}

							path_context.mLightEmission			= path_context.mThroughput * light_emission;
						}
					}
				}

				// Sample BSDF / [Mitsuba] BSDF sampling
				{
					BSDFContext bsdf_context					= BSDFEvaluation::GenerateContext(hit_context, path_context);
					BSDFResult bsdf_result						= BSDFEvaluation::Evaluate(bsdf_context, hit_context, path_context);
					
					path_context.mEmission						+= path_context.mThroughput * emission; // Emissive BSDF
					path_context.mThroughput					*= bsdf_result.mBSDFSamplePDF > 0 ? (bsdf_result.mBSDF * abs(bsdf_context.mNdotL) / bsdf_result.mBSDFSamplePDF) : 0;
					path_context.mEtaScale						*= bsdf_result.mEta;
					path_context.mMediumInstanceID				= bsdf_result.mMediumInstanceID; // [TODO] Need a medium stack to handle nested medium
					
					path_context.mPrevBSDFSamplePDF				= bsdf_result.mBSDFSamplePDF;
					path_context.mPrevDiracDeltaDistribution	= hit_context.DiracDeltaDistribution();

					DebugValue(DebugMode::BSDF__BSDF,			path_context.mRecursionDepth, float3(bsdf_result.mBSDF));
					DebugValue(DebugMode::BSDF__PDF,			path_context.mRecursionDepth, float3(bsdf_result.mBSDFSamplePDF, 0, 0));

					DebugValue(DebugMode::DiracDelta,			path_context.mRecursionDepth, float3(hit_context.DiracDeltaDistribution(), 0, 0));
					DebugValue(DebugMode::LobeIndex,			path_context.mRecursionDepth, float3(bsdf_result.mLobeIndex, 0, 0));

					// Prepare for next bounce
					ray.Origin									= hit_context.PositionWS();
					ray.Direction								= bsdf_context.mL;
					continue_bounce								= true;
				}
			}
			
			// DebugMode
			switch (mConstants.mVisualizeMode)
			{
			case VisualizeMode::None:			break;
			case VisualizeMode::Barycentrics: 	path_context.mEmission = hit_context.Barycentrics(); continue_bounce = false; break;
			case VisualizeMode::Position: 		path_context.mEmission = hit_context.PositionWS(); continue_bounce = false; break;
			case VisualizeMode::Normal: 		path_context.mEmission = hit_context.NormalWS(); continue_bounce = false; break;
			case VisualizeMode::UV:				path_context.mEmission = float3(hit_context.UV(), 0.0); continue_bounce = false; break;
			case VisualizeMode::Albedo: 		path_context.mEmission = hit_context.Albedo(); continue_bounce = false; break;
			case VisualizeMode::Reflectance: 	path_context.mEmission = hit_context.SpecularReflectance(); continue_bounce = false; break;
			case VisualizeMode::Emission: 		path_context.mEmission = hit_context.Emission(); continue_bounce = false; break;
			case VisualizeMode::RoughnessAlpha: path_context.mEmission = hit_context.RoughnessAlpha(); continue_bounce = false; break;
			case VisualizeMode::RecursionDepth:	continue_bounce = true; break;
			default:							path_context.mEmission = sVisualizeModeValue; continue_bounce = false; break;
			}
		}
		else
		{
			// Ray missed (Background)

			// Debug
			{
				if (mConstants.mDebugFlag & DebugFlag::UpdateRayInspection)
					if (ioPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && ioPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y)
						RayInspectionUAV[0].mPositionWS[path_context.mRecursionDepth + 1] = float4(ray.Origin + ray.Direction * 64.0, 0.0);

				if (path_context.mRecursionDepth == 0)
					if (ioPixelContext.mPixelIndex.x == mConstants.mPixelDebugCoord.x && ioPixelContext.mPixelIndex.y == mConstants.mPixelDebugCoord.y)
						PixelInspectionUAV[0].mPixelInstanceID = InvalidInstanceID;

				DebugValue(DebugMode::PositionWS, path_context.mRecursionDepth, float3(ray.Origin));
				DebugValue(DebugMode::DirectionWS, path_context.mRecursionDepth, float3(ray.Direction));
				DebugValue(DebugMode::InstanceID, path_context.mRecursionDepth, float3(-1.0, 0.0, 0.0));
			}

			float3 sky_luminance				= GetSkyLuminance(Ray::Generate(ray));

			float3 cloud_transmittance			= 1;
			float3 cloud_luminance				= 0;
		 	RaymarchCloud(Ray::Generate(ray), cloud_transmittance, cloud_luminance);

			float3 emission						= lerp(sky_luminance, cloud_luminance, 1.0 - cloud_transmittance);
			path_context.mEmission				+= path_context.mThroughput * emission;
			
			break;
		}
		
		DebugValue(DebugMode::Emission,			path_context.mRecursionDepth, path_context.mEmission);
		DebugValue(DebugMode::Throughput,		path_context.mRecursionDepth, float3(path_context.mThroughput));
		DebugValue(DebugMode::EtaScale,			path_context.mRecursionDepth, float3(path_context.mEtaScale, 0, 0));
		
		if (!continue_bounce)
			break;
		
		// Drop the ray if throughput is 0
		float throughput_max					= max(path_context.mThroughput.x, max(path_context.mThroughput.y, path_context.mThroughput.z));
		if (throughput_max <= 0)
			break;

		// Recursion Depth Count Max
		// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting.html
		// https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer
		if (path_context.mRecursionDepth + 1 > mConstants.mRecursionDepthCountMax)
			break;
		
		// [TODO] How should it be affect by Russian Roulette through MIS? Can not find anything related in Mitsuba.
		// LightEmission is kind of from next depth, but it does not look right to put this after Russian Roulette evaluation.
		path_context.mEmission					+= path_context.mLightEmission;
		path_context.mLightEmission				= 0;

		// Russian Roulette Depth
		if (path_context.mRecursionDepth + 1 > mConstants.mRussianRouletteDepth)
		{
			// Probability can be chosen in almost any manner
			// e.g. Fixed threshold
			// e.g. Veach's Efficiency-Optimized Russian roulette is based on average variance and cost
			float scale							= path_context.mEtaScale * path_context.mEtaScale; // See Dielectric::Evaluate
			float continue_probability			= min(throughput_max * scale, 0.95f);
			float probability					= RandomFloat01(path_context.mRandomState);
			bool probability_passed				= probability < continue_probability;

			DebugValue(DebugMode::RussianRoulette, path_context.mRecursionDepth, float3(probability_passed, probability, continue_probability));

			if (probability_passed)
				path_context.mThroughput		/= continue_probability; 				// Weight the path to keep result unbiased
			else
				break;																	// Termination by Russian Roulette
		}

		path_context.mRecursionDepth++;
	}

	// Accumulation
	{
		float3 current_output					= path_context.mEmission;
		float3 previous_output					= ScreenColorUAV[ioPixelContext.mPixelIndex.xy].xyz;
		previous_output							= max(0, previous_output); // Eliminate nan
		float3 mixed_output						= lerp(previous_output, current_output, mConstants.mCurrentFrameWeight);

		if (mConstants.mVisualizeMode != VisualizeMode::None)
			mixed_output						= current_output;

		if (mConstants.mVisualizeMode == VisualizeMode::RecursionDepth)
			mixed_output						= path_context.mRecursionDepth;

		ScreenColorUAV[ioPixelContext.mPixelIndex.xy] = float4(mixed_output, 1);
		
		if (sDebugValueUpdated)
			ScreenDebugUAV[ioPixelContext.mPixelIndex.xy] = sDebugValue;
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

	// Debug
	sDebugDispatchRaysIndex.xyz					= inDispatchThreadID.xyz;
	sDebugDispatchRaysDimensions				= uint3(output_dimensions.xy, 1);
	
	PixelContext pixel_context					= (PixelContext)0;
	pixel_context.mPixelIndex					= inDispatchThreadID.xyz;
	pixel_context.mPixelTotal					= uint3(output_dimensions.xy, 1);
	TraceRay(pixel_context);
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
void DepthPS(
	float4 inPosition : SV_POSITION,
	out float outDepth : SV_DEPTH)
{
	uint2 output_dimensions;
	ScreenColorUAV.GetDimensions(output_dimensions.x, output_dimensions.y);
	
	PixelContext pixel_context					= (PixelContext)0;
	pixel_context.mPixelIndex					= uint3(inPosition.xy, 1);
	pixel_context.mPixelTotal					= uint3(output_dimensions.xy, 1);
	pixel_context.mOutputDepth					= true;
	TraceRay(pixel_context);
	
	outDepth									= pixel_context.mDepth;
	
	if (sDebugValueUpdated)
		ScreenDebugUAV[inPosition.xy]			= sDebugValue;
}