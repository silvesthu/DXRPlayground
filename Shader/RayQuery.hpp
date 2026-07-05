
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Context.h"
#include "DebugUtils.h"
#include "BSDF.h"
#include "Light.h"
#include "Reservoir.h"
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
void TraceVisualRay(inout T ioQuery, inout RayDesc ioRay)
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
	
	uint random_state							= ioPixelContext.RandomSeed();

	float2 screen_coords						= float2(ioPixelContext.mPixelIndex.xy);
	float2 screen_size							= float2(ioPixelContext.mPixelTotal.xy);

	// [TODO] Need proper reconstruction filter, see https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Image_Reconstruction
	uint x = mConstants.mCurrentFrameIndex;
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
	Inspect::RayPrimary(ray);

	PathContext path_context					= (PathContext)0;
	path_context.mThroughput					= 1.0;
	path_context.mPrevBSDFSamplePDF				= 0.0;
	path_context.mPrevDiracDeltaDistribution	= true; // Allow primary ray to skip MIS
	path_context.mEtaScale						= 1.0;
	path_context.mRandomState					= random_state;
	path_context.mRecursionDepth				= 0;
	path_context.mMediumInstanceID				= InvalidInstanceID;
	
	Reservoir reservoir_to_write				= Reservoir::Generate();

	for (;;)
	{
		bool continue_bounce					= false;

		// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
		RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
		TraceVisualRay(query, ray);
		
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
						// [TODO] Support NEE on participating media
						// [TODO] Support phase function
						path_context.mThroughput					*= medium_context.Albedo();
						path_context.mPrevBSDFSamplePDF				= 1.0f / (4.0f * MATH_PI);
						path_context.mPrevDiracDeltaDistribution	= false;

						ray.Origin				= ray.Origin + ray.Direction * free_flight_distance;
						ray.Direction			= RandomUnitVector(path_context.mRandomState);

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

				Inspect::Update(InspectMode::MediumExtinction, path_context, medium_context.SigmaT());
				Inspect::Update(InspectMode::MediumFreeFlight, path_context, float3(free_flight_distance, query.CommittedRayT(), ray_scattered));
				Inspect::Update(InspectMode::MediumInstanceID, path_context, float3(path_context.mMediumInstanceID, 0, 0));
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
				Inspect::Hit(path_context, hit_context);

				// Emission
				float3 emission = hit_context.Emission() * (mConstants.mEmissionBoost * kPreExposure);
				{
					bool back_face				= dot(hit_context.mVertexNormalWS, hit_context.ViewWS()) < 0;
					bool two_sided				= hit_context.TwoSided() && false; // Mitsuba3's emitter does not become twosided even specified on bsdf
					if (back_face && !two_sided)
						emission = 0;
				}

				if (hit_context.BSDF() == BSDF::Light) // Ray hit a light / [Mitsuba] Direct emission
				{
					if (GetSampleMode() != SampleMode::Light || path_context.mRecursionDepth == 0)
					{
						float mis_weight				= 1.0f;
						if (GetSampleMode() == SampleMode::MIS &&
							!path_context.mPrevDiracDeltaDistribution && 			// Prev hit is not DiracDeltaDistribution, otherwise no NEE sample to MIS
							path_context.mMediumInstanceID == InvalidInstanceID &&	// Path is not inside medium
							true)
						{
							uint light_index			= hit_context.LightIndex();
							Light light					= RaytraceLightsSRV[light_index];

							// [TODO] Need update for ReSTIR
							LightContext light_context	= LightEvaluation::GenerateContext(LightEvaluation::ContextType::Input, ray.Direction, ContextConstant::sUVUnused, light_index, ray.Origin);
							float light_mis_pdf			= light_context.mSolidAnglePDF * LightContext::UniformSelectPDF();
					
							mis_weight					= max(0.0f, MIS::PowerHeuristic(1, path_context.mPrevBSDFSamplePDF, 1, light_mis_pdf));

							Inspect::Update(InspectMode::MIS_BSDF, path_context, float3(path_context.mPrevBSDFSamplePDF, light_mis_pdf, mis_weight), true);
						}
					
						path_context.mEmission			+= path_context.mThroughput * emission * mis_weight;
					}
				}
				else // Ray hit a surface
				{
					// Sample light (NEE) / [Mitsuba] Emitter sampling, before mThroughput updated
					bool sample_light = GetSampleMode() == SampleMode::Light || GetSampleMode() == SampleMode::MIS;
					if (mConstants.mLightCount > 0 &&											// No light -> no light sample
						!hit_context.DiracDeltaDistribution() &&								// Current hit is DiracDeltaDistribution -> no light sample
						sample_light &&															// BSDF mode -> no light sample
						path_context.mRecursionDepth < mConstants.mRecursionDepthCountMax &&	// Skip NEE for exceeding limit of recursion depth
						true)
					{
						const uint initial_sample_count		= max(1, mConstants.mReSTIR.mInitialSampleCount);
						Reservoir initial_reservoir			= Reservoir::Generate();
						LightContext initial_light_context	= (LightContext)0;
						for (uint initial_sample_index = 0; initial_sample_index < initial_sample_count; initial_sample_index++)
						{
							LightContext _light_context			= LightEvaluation::UniformSelect(hit_context.PositionWS(), path_context.mRandomState);
							BSDFContext _bsdf_context			= BSDFEvaluation::GenerateContext(BSDFContext::Mode::Light, _light_context.mL, hit_context, path_context);
							BSDFResult _bsdf_result				= BSDFEvaluation::Evaluate(_bsdf_context, hit_context, path_context);
							float _target_pdf					= _light_context.SamplePDF() > 0 ? (RGBToLuminance(_light_context.GetLight().mEmission * _bsdf_result.mBSDF) * abs(_bsdf_context.mNdotL) / _light_context.SamplePDF()) : 0.0f;

							// [TODO] Apply MIS on target_pdf, before resample
							float _source_pdf					= _light_context.UniformSelectPDF();
							float _blended_source_pdf			= _source_pdf;
							Reservoir _sample_reservoir			= Reservoir::FromLight(_light_context, _target_pdf, 1.0f / _blended_source_pdf);
							float _random01						= RandomFloat01(path_context.mRandomState);
							if (initial_reservoir.Stream(_sample_reservoir, 1.0, _random01)) // [TODO] Skip RNG on first sample
								initial_light_context			= _light_context;
						}
						initial_reservoir.ComputeContributionWeight(true);						
						if (ioPixelContext.mReservoirInitialize)
						{
							reservoir_to_write					= initial_reservoir;
							Inspect::R_Initial(path_context, reservoir_to_write);
							break;
						}
						if (ioPixelContext.mReservoirTemporal)
						{
							USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirInitializeUAV);
							USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirTemporalUAV);

							Reservoir reservoir					= Reservoir::Generate();
							Reservoir initial_reservoir			= Reservoir::Generate();
							Reservoir temporal_reservoir		= Reservoir::Generate();

							{
								
								initial_reservoir.Unpack(ScreenReservoirInitializeUAV[ioPixelContext.mPixelIndex.xy]);
								if (initial_reservoir.IsValid())
								{
									LightContext _light_context = LightEvaluation::GenerateContext(LightEvaluation::ContextType::UV, ContextConstant::sDirectionUnused, initial_reservoir.mUV, initial_reservoir.mLightIndex, hit_context.PositionWS());
									BSDFContext _bsdf_context	= BSDFEvaluation::GenerateContext(BSDFContext::Mode::Light, _light_context.mL, hit_context, path_context);
									BSDFResult _bsdf_result		= BSDFEvaluation::Evaluate(_bsdf_context, hit_context, path_context);
									float _target_pdf			= _light_context.SamplePDF() > 0 ? (RGBToLuminance(_light_context.GetLight().mEmission * _bsdf_result.mBSDF) * abs(_bsdf_context.mNdotL) / _light_context.SamplePDF()) : 0.0f;
									initial_reservoir.mTargetFunction = _target_pdf;
								}
							}

							bool temporal						= mConstants.mCurrentFrameIndex > 0 && mConstants.mReSTIR.mSampleCountTemporal != 0;
							if (temporal)
							{
								temporal_reservoir.Unpack(ScreenReservoirTemporalUAV[ioPixelContext.mPixelIndex.xy]);
								if (temporal_reservoir.IsValid())
								{
									LightContext _light_context = LightEvaluation::GenerateContext(LightEvaluation::ContextType::UV, ContextConstant::sDirectionUnused, temporal_reservoir.mUV, temporal_reservoir.mLightIndex, hit_context.PositionWS());
									BSDFContext _bsdf_context	= BSDFEvaluation::GenerateContext(BSDFContext::Mode::Light, _light_context.mL, hit_context, path_context);
									BSDFResult _bsdf_result		= BSDFEvaluation::Evaluate(_bsdf_context, hit_context, path_context);
									float _target_pdf			= _light_context.SamplePDF() > 0 ? (RGBToLuminance(_light_context.GetLight().mEmission * _bsdf_result.mBSDF) * abs(_bsdf_context.mNdotL) / _light_context.SamplePDF()) : 0.0f;
									temporal_reservoir.mTargetFunction = _target_pdf;
								}
								Inspect::R_Prev(path_context, temporal_reservoir);
							}

							reservoir.Stream(initial_reservoir, 1.0, kTrivialRandom01);
							reservoir.Stream(temporal_reservoir, 1.0, RandomFloat01(path_context.mRandomState));
							reservoir.ComputeContributionWeight(true);
							reservoir_to_write					= reservoir;
							Inspect::R_TemporalOut(path_context, reservoir_to_write);
							break;
						}
						if (ioPixelContext.mReservoirSpatial)
						{
							USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirInitializeUAV);
							USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirTemporalUAV);

							Reservoir reservoir				= Reservoir::Generate();
							Reservoir temporal_reservoir	= Reservoir::Generate();
							Reservoir spatial_reservoir		= Reservoir::Generate();

							temporal_reservoir.Unpack(ScreenReservoirTemporalUAV[ioPixelContext.mPixelIndex.xy]);
							if (temporal_reservoir.IsValid())
							{
								LightContext _light_context = LightEvaluation::GenerateContext(LightEvaluation::ContextType::UV, ContextConstant::sDirectionUnused, temporal_reservoir.mUV, temporal_reservoir.mLightIndex, hit_context.PositionWS());
								BSDFContext _bsdf_context	= BSDFEvaluation::GenerateContext(BSDFContext::Mode::Light, _light_context.mL, hit_context, path_context);
								BSDFResult _bsdf_result		= BSDFEvaluation::Evaluate(_bsdf_context, hit_context, path_context);
								float _target_pdf			= _light_context.SamplePDF() > 0 ? (RGBToLuminance(_light_context.GetLight().mEmission * _bsdf_result.mBSDF) * abs(_bsdf_context.mNdotL) / _light_context.SamplePDF()) : 0.0f;
								temporal_reservoir.mTargetFunction = _target_pdf;
							}

							for (uint sample_index = 0; sample_index < mConstants.mReSTIR.mSampleCountSpatial; sample_index++)
							{
								float r						= 5.0 * RandomFloat01(path_context.mRandomState);
								float theta					= 2.0 * MATH_PI * RandomFloat01(path_context.mRandomState);
								int x						= r * cos(theta);
								int y						= r * sin(theta);

								Reservoir _initial_reservoir = Reservoir::Generate();
								_initial_reservoir.Unpack(ScreenReservoirTemporalUAV[ioPixelContext.mPixelIndex.xy + int2(x, y)]);
								if (!_initial_reservoir.IsValid()) continue;

								LightContext _light_context = LightEvaluation::GenerateContext(LightEvaluation::ContextType::UV, ContextConstant::sDirectionUnused, _initial_reservoir.mUV, _initial_reservoir.mLightIndex, hit_context.PositionWS());
								BSDFContext _bsdf_context	= BSDFEvaluation::GenerateContext(BSDFContext::Mode::Light, _light_context.mL, hit_context, path_context);
								BSDFResult _bsdf_result		= BSDFEvaluation::Evaluate(_bsdf_context, hit_context, path_context);
								float _target_pdf			= _light_context.SamplePDF() > 0 ? (RGBToLuminance(_light_context.GetLight().mEmission * _bsdf_result.mBSDF) * abs(_bsdf_context.mNdotL) / _light_context.SamplePDF()) : 0.0f;

								_initial_reservoir.mTargetFunction	= _target_pdf;
								float _random01						= RandomFloat01(path_context.mRandomState);
								spatial_reservoir.Stream(_initial_reservoir, 1.0, _random01);
							}
							spatial_reservoir.ComputeContributionWeight(true);

							reservoir.Stream(temporal_reservoir, 1.0, kTrivialRandom01);
							reservoir.Stream(spatial_reservoir, 1.0, RandomFloat01(path_context.mRandomState));
							reservoir.ComputeContributionWeight(true);
							reservoir_to_write				= reservoir;
							Inspect::R_Spatial(path_context, reservoir_to_write);
							break;
						}

						Reservoir reservoir					= initial_reservoir;
						LightContext light_context			= initial_light_context;
						if (ioPixelContext.mReservoirUse && mConstants.mReSTIR.mEnabled)
						{
							USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirSpatialUAV);

							Reservoir spatial_reservoir		= Reservoir::Generate();
							spatial_reservoir.Unpack(ScreenReservoirSpatialUAV[ioPixelContext.mPixelIndex.xy]);
							if (spatial_reservoir.IsValid())
							{
								LightContext _light_context = LightEvaluation::GenerateContext(LightEvaluation::ContextType::UV, ContextConstant::sDirectionUnused, spatial_reservoir.mUV, spatial_reservoir.mLightIndex, hit_context.PositionWS());
								BSDFContext _bsdf_context	= BSDFEvaluation::GenerateContext(BSDFContext::Mode::Light, _light_context.mL, hit_context, path_context);
								BSDFResult _bsdf_result		= BSDFEvaluation::Evaluate(_bsdf_context, hit_context, path_context);
								float _target_pdf			= _light_context.SamplePDF() > 0 ? (RGBToLuminance(_light_context.GetLight().mEmission * _bsdf_result.mBSDF) * abs(_bsdf_context.mNdotL) / _light_context.SamplePDF()) : 0.0f;
								spatial_reservoir.mTargetFunction = _target_pdf;

								light_context				= _light_context;
							}
							reservoir						= spatial_reservoir;
						}

						Inspect::SampleLight(path_context, light_context);
						bool light_visible = false;
						if (reservoir.IsValid())
						{
							RayDesc shadow_ray;
							shadow_ray.Origin			= hit_context.PositionWS();
							shadow_ray.Direction		= light_context.mL;
							shadow_ray.TMin				= 1E-4;
							shadow_ray.TMax				= 10000;

							RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadow_query;
							TraceShadowRay(shadow_query, shadow_ray);

							// Shadow ray hit the light
							light_visible = IsHit(shadow_query) && shadow_query.CommittedInstanceID() == light_context.GetLight().mInstanceID;

							Inspect::HitLight(path_context, shadow_ray.Origin + shadow_ray.Direction * shadow_query.CommittedRayT());
						}
						else
						{
							reservoir					= Reservoir::Generate();
						}
						
						if (light_visible)
						{
							BSDFContext bsdf_context	= BSDFEvaluation::GenerateContext(BSDFContext::Mode::Light, light_context.mL, hit_context, path_context);
							BSDFResult bsdf_result		= BSDFEvaluation::Evaluate(bsdf_context, hit_context, path_context);

							float3 luminance			= light_context.GetLight().mEmission * (mConstants.mEmissionBoost * kPreExposure);
							float3 light_emission		= luminance * bsdf_result.mBSDF * abs(bsdf_context.mNdotL) / light_context.mSolidAnglePDF * reservoir.mContributionWeight;

							if (GetSampleMode() == SampleMode::MIS)
							{
								float bsdf_mis_pdf		= bsdf_result.mBSDFSamplePDF;
								float light_mis_pdf		= light_context.SamplePDF();
								float mis_weight		= max(0.0f, MIS::PowerHeuristic(1, light_mis_pdf, 1, bsdf_mis_pdf));
								light_emission			*= mis_weight;

								Inspect::Update(InspectMode::MIS_LIGHT, path_context, float3(bsdf_mis_pdf, light_mis_pdf, mis_weight));
							}

							if (bsdf_result.mMediumInstanceID == InvalidInstanceID) // Shadow ray not support medium yet
							{
								path_context.mEmission	+= path_context.mThroughput * light_emission;
							}

							Inspect::BSDF(path_context, bsdf_context);
							Inspect::SampleLightResult(path_context, bsdf_context, bsdf_result, light_context.SamplePDF());
						}
						else
						{
							// Should not reset reseroivr as visibility is not part of target
						}
					}

					// Sample BSDF / [Mitsuba] BSDF sampling
					{
						BSDFContext bsdf_context					= BSDFEvaluation::GenerateContext(BSDFContext::Mode::BSDF, ContextConstant::sDirectionUnused, hit_context, path_context);
						BSDFResult bsdf_result						= BSDFEvaluation::Evaluate(bsdf_context, hit_context, path_context);
					
						path_context.mEmission						+= path_context.mThroughput * emission; // Emissive BSDF
						path_context.mThroughput					*= bsdf_result.mBSDFSamplePDF > 0 ? (bsdf_result.mBSDF * abs(bsdf_context.mNdotL) / bsdf_result.mBSDFSamplePDF) : 0;
						path_context.mEtaScale						*= bsdf_result.mEta;
						path_context.mMediumInstanceID				= bsdf_result.mMediumInstanceID;
					
						path_context.mPrevBSDFSamplePDF				= bsdf_result.mBSDFSamplePDF;
						path_context.mPrevDiracDeltaDistribution	= hit_context.DiracDeltaDistribution();

						// Prepare for next bounce
						ray.Origin									= hit_context.PositionWS();
						ray.Direction								= bsdf_context.mL;
						continue_bounce								= true;

						Inspect::BSDF(path_context, bsdf_context);
						Inspect::SampleBSDFResult(path_context, hit_context, bsdf_context, bsdf_result);
					}
				}

				if (mConstants.mSpatialCache.mFrameActive)
					SpatialCache::AddData(SpatialCache::FindOrInsert(hit_context.PositionWS(), 0, SpatialCache::kCellSize), 1);
				
				Visualize::Hit(path_context, hit_context, continue_bounce);
				// PrintNameValueLine("Albedo: ", hit_context.Albedo());
			}
		}
		else
		{
			// Ray missed (Background)
			Inspect::Miss(path_context, ray);

			float3 sky_luminance				= GetSkyLuminance(Ray::Generate(ray, 0.0f));

			float3 cloud_transmittance			= 1;
			float3 cloud_luminance				= 0;
		 	RaymarchCloud(Ray::Generate(ray, 0.0f), cloud_transmittance, cloud_luminance);

			float3 emission						= lerp(sky_luminance, cloud_luminance, 1.0 - cloud_transmittance);
			path_context.mEmission				+= path_context.mThroughput * emission;

			break;
		}

		Inspect::Update(InspectMode::Emission,	path_context, path_context.mEmission);
		Inspect::Update(InspectMode::Throughput,	path_context, float3(path_context.mThroughput));
		Inspect::Update(InspectMode::EtaScale,	path_context, float3(path_context.mEtaScale, 0, 0));

		if (!continue_bounce)
			break;

		// Recursion Depth Count Max
		// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting.html
		// https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer
		if (path_context.mRecursionDepth + 1 > mConstants.mRecursionDepthCountMax)
			break;

		// Drop the ray if throughput is 0
		float throughput_max					= max(path_context.mThroughput.x, max(path_context.mThroughput.y, path_context.mThroughput.z));
		if (throughput_max <= 0)
			break;

		// Russian Roulette
		if (path_context.mRecursionDepth + 1 > mConstants.mRussianRouletteDepth)
		{
			// Probability can be chosen in almost any manner
			// e.g. Fixed threshold
			// e.g. Veach's Efficiency-Optimized Russian roulette is based on average variance and cost
			float scale							= path_context.mEtaScale * path_context.mEtaScale; // See Dielectric::Evaluate
			float continue_probability			= min(throughput_max * scale, 0.95f);
			float probability					= RandomFloat01(path_context.mRandomState);
			bool probability_passed				= probability < continue_probability;

			Inspect::Update(InspectMode::RussianRoulette, path_context, float3(probability_passed, probability, continue_probability));

			if (probability_passed)
				path_context.mThroughput		/= continue_probability; 				// Weight the path to keep result unbiased
			else
				break;																	// Termination by Russian Roulette
		}

		path_context.mRecursionDepth++;
	}

	if (ioPixelContext.mReservoirInitialize)
	{
		USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirInitializeUAV);
		if (mConstants.mCurrentFrameWeight != 0.0f)
			ScreenReservoirInitializeUAV[ioPixelContext.mPixelIndex.xy] = reservoir_to_write.Pack();
		return;
	}
	if (ioPixelContext.mReservoirTemporal)
	{
		USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirTemporalUAV);
		if (mConstants.mCurrentFrameWeight != 0.0f)
			ScreenReservoirTemporalUAV[ioPixelContext.mPixelIndex.xy] = reservoir_to_write.Pack();
		return;
	}
	if (ioPixelContext.mReservoirSpatial)
	{
		USING_RESOURCE(RWTexture2D<uint4>, ScreenReservoirSpatialUAV);
		if (mConstants.mCurrentFrameWeight != 0.0f)
			ScreenReservoirSpatialUAV[ioPixelContext.mPixelIndex.xy] = reservoir_to_write.Pack();
		return;
	}

	// Accumulation
	float3 screen_color_current					= path_context.mEmission;
	float3 screen_color							= screen_color_current;

	if (mConstants.mAccumulationMode == AccumulationMode::Average)
	{	
		float3 screen_color_previous			= ScreenColorUAV[ioPixelContext.mPixelIndex.xy].xyz;
		screen_color_previous					= max(0, screen_color_previous); // Eliminate nan
		screen_color							= lerp(screen_color_previous, screen_color_current, mConstants.mCurrentFrameWeight);
	}

	switch (GetVisualizeMode())
	{
	case VisualizeMode::None: break;
	case VisualizeMode::RecursionDepth: screen_color = GetDebugRecursion() == 0 ? path_context.mRecursionDepth : (GetDebugRecursion() == path_context.mRecursionDepth); break;
	case VisualizeMode::RandomState:	screen_color = path_context.mRecursionDepth < GetDebugRecursion() ? 0 : pow(path_context.mRandomState / 4294967296.0, 2.0); break;
	default:							screen_color = screen_color_current; break;
	}

	ScreenColorUAV[ioPixelContext.mPixelIndex.xy] = float4(screen_color, 1);
}

[numthreads(8, 8, 1)]
void RayQueryCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(RWTexture2D<float4>, ScreenColorUAV);

	InstanceDataCache::Initialize(inGroupIndex);
	GroupMemoryBarrierWithGroupSync();

	uint2 output_dimensions;
	ScreenColorUAV.GetDimensions(output_dimensions.x, output_dimensions.y);

	PixelContext pixel_context					= (PixelContext)0;
	pixel_context.mPixelIndex					= inDispatchThreadID.xyz;
	pixel_context.mPixelTotal					= uint3(output_dimensions.xy, 1);
	pixel_context.mReservoirUse					= true;

	Inspect::Initialize(pixel_context);
	ShaderPrint::Initialize(pixel_context);
	TraceRay(pixel_context);
}

[numthreads(8, 8, 1)]
void ReservoirInitializeCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(RWTexture2D<float4>, ScreenColorUAV);

	InstanceDataCache::Initialize(inGroupIndex);
	GroupMemoryBarrierWithGroupSync();

	uint2 output_dimensions;
	ScreenColorUAV.GetDimensions(output_dimensions.x, output_dimensions.y);

	PixelContext pixel_context = (PixelContext)0;
	pixel_context.mPixelIndex = inDispatchThreadID.xyz;
	pixel_context.mPixelTotal = uint3(output_dimensions.xy, 1);
	pixel_context.mReservoirInitialize = true;

	Inspect::Initialize(pixel_context);
	ShaderPrint::Initialize(pixel_context);
	TraceRay(pixel_context);
}

[numthreads(8, 8, 1)]
void ReservoirTemporalCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(RWTexture2D<float4>, ScreenColorUAV);

	InstanceDataCache::Initialize(inGroupIndex);
	GroupMemoryBarrierWithGroupSync();

	uint2 output_dimensions;
	ScreenColorUAV.GetDimensions(output_dimensions.x, output_dimensions.y);

	PixelContext pixel_context = (PixelContext)0;
	pixel_context.mPixelIndex = inDispatchThreadID.xyz;
	pixel_context.mPixelTotal = uint3(output_dimensions.xy, 1);
	pixel_context.mReservoirTemporal = true;

	Inspect::Initialize(pixel_context);
	ShaderPrint::Initialize(pixel_context);
	TraceRay(pixel_context);
}

[numthreads(8, 8, 1)]
void ReservoirSpatialCS(COMPUTE_SHADER_INPUT)
{
	USING_RESOURCE(RWTexture2D<float4>, ScreenColorUAV);

	InstanceDataCache::Initialize(inGroupIndex);
	GroupMemoryBarrierWithGroupSync();

	uint2 output_dimensions;
	ScreenColorUAV.GetDimensions(output_dimensions.x, output_dimensions.y);

	PixelContext pixel_context = (PixelContext)0;
	pixel_context.mPixelIndex = inDispatchThreadID.xyz;
	pixel_context.mPixelTotal = uint3(output_dimensions.xy, 1);
	pixel_context.mReservoirSpatial = true;

	Inspect::Initialize(pixel_context);
	ShaderPrint::Initialize(pixel_context);
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

	Inspect::Initialize(pixel_context);
	ShaderPrint::Initialize(pixel_context);
	TraceRay(pixel_context);
	
	outDepth									= pixel_context.mDepth;
}
