
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Context.h"
#include "DebugUtils.h"
#include "BSDF.h"
#include "Light.h"
#include "AtmosphereIntegration.h"
#include "CloudIntegration.h"
#include "SpatialCache.h"
#include "ShaderPrint.h"

#if NVAPI_LSS
#define IsHit(query) (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT || NvRtCommittedIsLss(query) || NvRtCommittedIsSphere(query))
#else
#define IsHit(query) (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
#endif // NVAPI_LSS

template<typename T>
void TracePrimaryRay(inout T ioQuery, inout RayDesc ioRay)
{
	USING_RESOURCE(RaytracingAccelerationStructure, RaytraceTLASSRV);

	uint additional_ray_flags = 0;
	uint ray_instance_mask = 0xffffffff;
	ioQuery.TraceRayInline(RaytraceTLASSRV, additional_ray_flags, ray_instance_mask, ioRay);
	ioQuery.Proceed();
}

template<typename T>
void TraceShadowRay(inout T ioQuery, inout RayDesc ioRay)
{
	USING_RESOURCE(RaytracingAccelerationStructure, RaytraceTLASSRV);

	uint additional_ray_flags = 0;
	uint ray_instance_mask = 0xffffffff;
	ioQuery.TraceRayInline(RaytraceTLASSRV, additional_ray_flags, ray_instance_mask, ioRay);
	ioQuery.Proceed();
}

void TraceRay(inout PixelContext ioPixelContext)
{
	USING_RESOURCE(RWTexture2D<float4>, ScreenColorUAV);
	USING_RESOURCE(StructuredBuffer<Light>, RaytraceLightsSRV);

	DebugValueInit();
	sShaderPrint.Init(ioPixelContext);
	
	// From https://www.shadertoy.com/view/tsBBWW
	// [TODO] Need proper noise
	uint random_state							= uint(uint(ioPixelContext.mPixelIndex.x) * uint(1973) + uint(ioPixelContext.mPixelIndex.y) * uint(9277) + uint(mConstants.mCurrentFrameIndex) * uint(26699)) | uint(1);
	uint random_state_restir					= uint(uint(ioPixelContext.mPixelIndex.x) * uint(1973) + uint(ioPixelContext.mPixelIndex.y) * uint(9277) + uint(mConstants.mReSTIR.mTemporalFrameIndex) * uint(26699)) | uint(1);

	float2 screen_coords						= float2(ioPixelContext.mPixelIndex.xy);
	float2 screen_size							= float2(ioPixelContext.mPixelTotal.xy);

	// [TODO] Need proper reconstruction filter, see https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Image_Reconstruction
	switch (GetOffsetMode())
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
	ray.TMax									= 10000;

	PathContext path_context					= (PathContext)0;
	path_context.mThroughput					= 1.0;
	path_context.mPrevBSDFSamplePDF				= 0.0;
	path_context.mPrevLobeIndex					= 0;
	path_context.mPrevDiracDeltaDistribution	= false;
	path_context.mEtaScale						= 1.0;
	path_context.mRandomState					= random_state;
	path_context.mRandomStateReSTIR				= random_state_restir;
	path_context.mRecursionDepth				= 0;
	path_context.mMediumInstanceID				= InvalidInstanceID;

	InspectRay::Primary(ioPixelContext, path_context, ray);

	for (;;)
	{
		bool continue_bounce					= false;

		// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
		RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
		TracePrimaryRay(query, ray);
		
		if (ioPixelContext.mOutputDepth)
		{
			if (IsHit(query))
			{
				float4 position_ws				= float4(ray.Origin + ray.Direction * query.CommittedRayT(), 1.0);
				float4 position_ps				= mul(mConstants.mViewProjectionMatrix, position_ws);
				float4 position_ndc				= position_ps.xyzw / position_ps.w;
				
				ioPixelContext.mDepth			= position_ndc.z;
			}
			else
			{
				ioPixelContext.mDepth			= 1.0;
			}
			return;
		}
		
		if (IsHit(query))
		{
			// Participating media (Medium)
			// [TODO] Need a medium stack to handle nested medium
			// [TODO] Skip medium in case ray is offseted to outside, or mesh is not water tight
			bool ray_scattered					= false;
			MediumContext medium_context		= MediumContext::Generate(ray, query);
			if (path_context.mMediumInstanceID != InvalidInstanceID && medium_context.mInstanceID == path_context.mMediumInstanceID)
			{
				uint channel					= (uint)clamp(RandomFloat01(path_context.mRandomState) * 3.0, 0, 2);

				// [TODO] RGB/Spectrum sampling
				// Mitsuba3 take a random channel with uniform sampling (?), see VolumetricPathIntegrator::sample
				float inv_majorant_extinction	= 1.0 / max(medium_context.mMajorantSigmaT[channel], 1E-6);
				float free_flight_distance		= 0;

				const bool loop_until_event		= true;

				do
				{
					free_flight_distance		+= -log(1.0 - RandomFloat01(path_context.mRandomState)) * inv_majorant_extinction;
					if (free_flight_distance >= query.CommittedRayT())
						break;

					medium_context.ScatterAt(free_flight_distance, path_context);

					bool null_scattering		= RandomFloat01(path_context.mRandomState) >= medium_context.SigmaT()[channel] * inv_majorant_extinction;
					if (!null_scattering)
					{
						path_context.mThroughput *= medium_context.Albedo();

						ray.Origin				= ray.Origin + ray.Direction * free_flight_distance;
						ray.Direction			= RandomUnitVector(path_context.mRandomState); // [TODO] Support phase function

						ray_scattered			= true;
						continue_bounce			= true;

						break;
					}
					else if (!loop_until_event)
					{
						ray.Origin				= ray.Origin + ray.Direction * free_flight_distance;
						ray_scattered			= true;
						continue_bounce			= true;
					}

				} while (loop_until_event);

				InspectPixel::Update(DebugMode::MediumExtinction, path_context, medium_context.SigmaT());
				InspectPixel::Update(DebugMode::MediumFreeFlight, path_context, float3(free_flight_distance, query.CommittedRayT(), ray_scattered));
				InspectPixel::Update(DebugMode::MediumInstanceID, path_context, float3(path_context.mMediumInstanceID, 0, 0));
			}

			// Participating media (Atmosphere)
			{
				float3 in_scattering			= 0;
				float3 transmittance			= 1;

				GetSkyLuminanceToPoint(Ray::Generate(ray, query.CommittedRayT()), in_scattering, transmittance);

				path_context.mEmission			+= in_scattering;
				path_context.mThroughput		*= transmittance;

				VisualizeValue(VisualizeMode::InScattering, in_scattering);
				VisualizeValue(VisualizeMode::Transmittance, transmittance);
			}
			
			if (ray_scattered)
			{
				// To next bounce
			}
			else 
			{
				// HitContext
				HitContext hit_context			= HitContext::Generate(ray, query);
				InspectRay::Hit(ioPixelContext, path_context, hit_context);

				// Emission
				float3 emission = hit_context.Emission() * (mConstants.mEmissionBoost * kPreExposure);
				{
					bool back_face				= dot(hit_context.mVertexNormalWS, hit_context.ViewWS()) < 0;
					// bool two_sided				= hit_context.TwoSided();
					bool two_sided				= false; // Mitsuba3's emitter does not become twosided even specified on bsdf
					if (back_face && !two_sided)
						emission = 0;

					if (GetDebugInstanceMode() == DebugInstanceMode::Barycentrics && GetDebugInstanceIndex() == hit_context.mInstanceID)
						emission = hit_context.mBarycentrics;
				}

				if (hit_context.BSDF() == BSDF::Light) // Ray hit a light / [Mitsuba] Direct emission
				{
					if (path_context.mRecursionDepth == 0 ||					// Camera ray hit the light
						path_context.mPrevDiracDeltaDistribution || 			// Prev hit is DiracDeltaDistribution -> no light sample
						GetSampleMode() == SampleMode::SampleBSDF ||			// SampleBSDF mode -> no light sample
						false)
					{
						// Add light contribution
					
						path_context.mEmission						+= path_context.mThroughput * emission;
					
						if (path_context.mRecursionDepth == 0)
							InspectPixel::Update(DebugMode::LightIndex, path_context, float3(hit_context.LightIndex() + 0.5, 0, 0)); // Add a offset to identify light source in LightIndex debug output
					}
					else if (GetSampleMode() == SampleMode::MIS)
					{
						// Add light contribution with MIS
					
						// Select light
						uint light_index							= hit_context.LightIndex();
						Light light									= RaytraceLightsSRV[light_index];

						// [TODO] Need update for ReSTIR
						LightContext light_context					= LightEvaluation::GenerateContext(LightEvaluation::ContextType::Input, ray.Direction, light_index, ray.Origin, path_context);
						float light_mis_pdf							= light_context.mSolidAnglePDF * LightContext::UniformSelectionPDF();
					
						float mis_weight							= max(0.0f, MIS::PowerHeuristic(1, path_context.mPrevBSDFSamplePDF, 1, light_mis_pdf));
						path_context.mEmission						+= path_context.mThroughput * emission * mis_weight;
					
						InspectPixel::Update(DebugMode::MIS_BSDF, path_context, float3(path_context.mPrevBSDFSamplePDF, light_mis_pdf, mis_weight), true);
					}
				}
				else // Ray hit a surface
				{
					// Sample light (NEE) / [Mitsuba] Emitter sampling
					bool sample_light = GetSampleMode() == SampleMode::SampleLight || GetSampleMode() == SampleMode::MIS;
					if (mConstants.mLightCount > 0 &&							// No light -> no light sample
						!hit_context.DiracDeltaDistribution() &&				// Current hit is DiracDeltaDistribution -> no light sample
						sample_light &&											// SampleBSDF mode -> no light sample
						true)
					{
						// Select light
						LightContext light_context					= LightEvaluation::SelectLight(hit_context.PositionWS(), path_context);

						InspectPixel::Update(DebugMode::LightIndex,	path_context, float3(light_context.LightIndex(), 0.0, 0.0));
						InspectPixel::Update(DebugMode::RIS_SAMPLE,	path_context, float3(light_context.mReservoir.mTargetPDF, 0.0, 0.0));
						InspectPixel::Update(DebugMode::RIS_SUM,	path_context, float3(light_context.mReservoir.mWeightSum, light_context.mReservoir.mCountSum, 0.0));
					
						float light_weight = light_context.mSolidAnglePDF <= 0.0 ? 0.0 : (light_context.SelectionWeight() / light_context.mSolidAnglePDF);
						if (light_context.IsValid() && light_weight > 0)
						{
							// Cast shadow ray
							RayDesc shadow_ray;
							shadow_ray.Origin						= hit_context.PositionWS();
							shadow_ray.Direction					= light_context.mL;
							shadow_ray.TMin							= 1E-4;
							shadow_ray.TMax							= 10000;

							RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadow_query;
							TraceShadowRay(shadow_query, shadow_ray);
						
							// Shadow ray hit the light
							if (IsHit(shadow_query) && shadow_query.CommittedInstanceID() == light_context.GetLight().mInstanceID)
							{
								InspectRay::HitLight(ioPixelContext, path_context, shadow_ray.Origin + shadow_ray.Direction * shadow_query.CommittedRayT());
							
								BSDFContext bsdf_context			= BSDFContext::Generate(BSDFContext::Mode::Light, light_context.mL, hit_context);
								BSDFResult bsdf_result				= BSDFEvaluation::Evaluate(bsdf_context, hit_context, path_context);
							
								InspectPixel::Update(DebugMode::Light_L, 	path_context, float3(bsdf_context.mL));
								InspectPixel::Update(DebugMode::Light_V, 	path_context, float3(bsdf_context.mV));
								InspectPixel::Update(DebugMode::Light_N, 	path_context, float3(bsdf_context.mN));
								InspectPixel::Update(DebugMode::Light_H, 	path_context, float3(bsdf_context.mH));

								InspectPixel::Update(DebugMode::Light_BSDF,	path_context, float3(bsdf_result.mBSDF));
								InspectPixel::Update(DebugMode::Light_PDF,	path_context, float3(1.0 / light_weight, 0, 0));

								float3 luminance					= light_context.GetLight().mEmission * (mConstants.mEmissionBoost * kPreExposure);
								float3 light_emission				= luminance * bsdf_result.mBSDF * abs(bsdf_context.mNdotL) * light_weight;

								if (GetSampleMode() == SampleMode::MIS)
								{
									float bsdf_mis_pdf				= bsdf_result.mBSDFSamplePDF;
									float light_mis_pdf				= light_context.MISPDF();
									float mis_weight				= max(0.0f, MIS::PowerHeuristic(1, light_mis_pdf, 1, bsdf_mis_pdf));
									light_emission					*= mis_weight;
								
									InspectPixel::Update(DebugMode::MIS_LIGHT, path_context, float3(bsdf_mis_pdf, light_mis_pdf, mis_weight));
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
						path_context.mPrevLobeIndex					= bsdf_result.mLobeIndex;
						path_context.mPrevDiracDeltaDistribution	= hit_context.DiracDeltaDistribution();

						InspectPixel::Update(DebugMode::BSDF__BSDF,	path_context, float3(bsdf_result.mBSDF));
						InspectPixel::Update(DebugMode::BSDF__PDF,	path_context, float3(bsdf_result.mBSDFSamplePDF, 0, 0));

						InspectPixel::Update(DebugMode::DiracDelta,	path_context, float3(hit_context.DiracDeltaDistribution(), 0, 0));
						InspectPixel::Update(DebugMode::LobeIndex,	path_context, float3(bsdf_result.mLobeIndex, 0, 0));

						// Prepare for next bounce
						ray.Origin									= hit_context.PositionWS();
						ray.Direction								= bsdf_context.mL;
						continue_bounce								= true;
					}
				}

				if (mConstants.mSpatialCache.mFrameActive)
					SpatialCache::AddData(SpatialCache::FindOrInsert(hit_context.PositionWS(), 0, SpatialCache::kCellSize), 1);

				// DebugMode
				switch (GetVisualizeMode())
				{
				case VisualizeMode::None:							break;
				case VisualizeMode::PrimitiveIndex:					path_context.mEmission = IntToColor((hit_context.mInstanceID << 16) + hit_context.mPrimitiveIndex + 1 /* skip 0 = black */); continue_bounce = false; break;
				case VisualizeMode::ClusterID:						path_context.mEmission = IntToColor((hit_context.mInstanceID << 16) + hit_context.mClusterID + 1 /* skip 0 = black */); continue_bounce = false; break;
				case VisualizeMode::Barycentrics: 					path_context.mEmission = hit_context.Barycentrics(); continue_bounce = false; break;
				case VisualizeMode::Position: 						path_context.mEmission = hit_context.PositionWS(); continue_bounce = false; break;
				case VisualizeMode::Normal: 						path_context.mEmission = hit_context.NormalWS(); continue_bounce = false; break;
				case VisualizeMode::UV:								path_context.mEmission = float3(hit_context.UV(), 0.0); continue_bounce = false; break;
				case VisualizeMode::Albedo: 						path_context.mEmission = hit_context.Albedo(); continue_bounce = false; break;
				case VisualizeMode::Reflectance: 					path_context.mEmission = hit_context.SpecularReflectance(); continue_bounce = false; break;
				case VisualizeMode::Emission: 						path_context.mEmission = hit_context.Emission(); continue_bounce = false; break;
				case VisualizeMode::RoughnessAlpha:					path_context.mEmission = hit_context.RoughnessAlpha(); continue_bounce = false; break;
				case VisualizeMode::RecursionDepth:					continue_bounce = true; break;
				case VisualizeMode::RandomState:					continue_bounce = path_context.mRecursionDepth <= GetDebugRecursion(); break;
				case VisualizeMode::SpatialHash:					path_context.mEmission = SpatialCache::HashGridGetColorFromHash32(SpatialCache::FindOrInsert(hit_context.PositionWS(), 0, SpatialCache::kCellSize)); continue_bounce = false; break;
				case VisualizeMode::SpatialData:					path_context.mEmission = SpatialCache::LoadData(SpatialCache::FindOrInsert(hit_context.PositionWS(), 0, SpatialCache::kCellSize)) / 1024.0; continue_bounce = false; break;
				default:											path_context.mEmission = sVisualizeModeValue; continue_bounce = false; break;
				}

				// ShaderPrint
				// PrintNameValueLine("Albedo:", hit_context.Albedo());
			}
		}
		else
		{
			// Ray missed (Background)
			InspectRay::Miss(ioPixelContext, path_context, ray);

			float3 sky_luminance				= GetSkyLuminance(Ray::Generate(ray, 0.0f));

			float3 cloud_transmittance			= 1;
			float3 cloud_luminance				= 0;
		 	RaymarchCloud(Ray::Generate(ray, 0.0f), cloud_transmittance, cloud_luminance);

			float3 emission						= lerp(sky_luminance, cloud_luminance, 1.0 - cloud_transmittance);
			path_context.mEmission				+= path_context.mThroughput * emission;
			
			break;
		}
		
		InspectPixel::Update(DebugMode::Emission,	path_context, path_context.mEmission);
		InspectPixel::Update(DebugMode::Throughput,	path_context, float3(path_context.mThroughput));
		InspectPixel::Update(DebugMode::EtaScale,	path_context, float3(path_context.mEtaScale, 0, 0));
		
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

			InspectPixel::Update(DebugMode::RussianRoulette, path_context, float3(probability_passed, probability, continue_probability));

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

		if (GetVisualizeMode() != VisualizeMode::None)
			mixed_output						= current_output;

		if (GetVisualizeMode() == VisualizeMode::RecursionDepth)
			mixed_output						= GetDebugRecursion() == 0 ? path_context.mRecursionDepth : (GetDebugRecursion() == path_context.mRecursionDepth);

		if (GetVisualizeMode() == VisualizeMode::RandomState)
			mixed_output						= path_context.mRecursionDepth < GetDebugRecursion() ? 0 : pow(path_context.mRandomState / 4294967296.0, 2.0);

		ScreenColorUAV[ioPixelContext.mPixelIndex.xy] = float4(mixed_output, 1);
		
		if (sDebugValueUpdated)
			WriteScreenDebugUAV(ioPixelContext.mPixelIndex.xy, sDebugValue);
	}
}

[numthreads(8, 8, 1)]
void RayQueryCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(RWTexture2D<float4>, ScreenColorUAV);

	InstanceDataCache::Initialize(inGroupIndex);
	GroupMemoryBarrierWithGroupSync();

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

void DepthPS(
	float4 inPosition : SV_POSITION,
	out float outDepth : SV_DEPTH)
{
	USING_RESOURCE(RWTexture2D<float4>, ScreenColorUAV);

	uint2 output_dimensions;
	ScreenColorUAV.GetDimensions(output_dimensions.x, output_dimensions.y);
	
	PixelContext pixel_context					= (PixelContext)0;
	pixel_context.mPixelIndex					= uint3(inPosition.xy, 1);
	pixel_context.mPixelTotal					= uint3(output_dimensions.xy, 1);
	pixel_context.mOutputDepth					= true;
	TraceRay(pixel_context);
	
	outDepth									= pixel_context.mDepth;
	
	if (sDebugValueUpdated)
		WriteScreenDebugUAV(inPosition.xy, sDebugValue);
}
