#pragma once
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

namespace BSDFEvaluation
{
	BSDF GetBSDF(HitContext inHitContext)
	{
		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID)
		{
			switch (mConstants.mDebugInstanceMode)
			{
			case DebugInstanceMode::Barycentrics: return BSDF::DebugEmissive;
			case DebugInstanceMode::Mirror: return BSDF::DebugMirror;
			default: break;
			}
		}

		return InstanceDatas[inHitContext.mInstanceID].mBSDF;
	}

	bool DiracDeltaDistribution(HitContext inHitContext)
	{
		switch (GetBSDF(inHitContext))
		{
		case BSDF::Dielectric:			return true;
		case BSDF::ThinDielectric:		return true;
		case BSDF::DebugMirror:			return true;
		default:						return false;
		}
	}

	BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inLight, float3 inNormal, float3 inView, float inEta, HitContext inHitContext)
	{
		// [NOTE] All vectors should be in the same space
		//        [Mitsuba3] Does all calculation in tangent space where N = (0,0,1), then e.g NdotL = L.z

		BSDFContext bsdf_context;

		bsdf_context.mMode = inMode;

		bsdf_context.mL = inLight;
		bsdf_context.mN = inNormal;
		bsdf_context.mV = inView;

		// Handle TwoSided
		if (dot(bsdf_context.mN, bsdf_context.mV) < 0 && InstanceDatas[inHitContext.mInstanceID].mTwoSided)
			bsdf_context.mN = -bsdf_context.mN;

		float3 V = bsdf_context.mV;
		if (inMode == BSDFContext::Mode::BSDFSample)
			V = dot(bsdf_context.mN, bsdf_context.mV) < 0 ? -V : V;
		bsdf_context.mH = normalize(V + bsdf_context.mL * inEta);

		// [TODO] Need stricter handling of side of vectors

		bsdf_context.mNdotV = dot(bsdf_context.mN, bsdf_context.mV);
		bsdf_context.mNdotL = dot(bsdf_context.mN, bsdf_context.mL);
		bsdf_context.mNdotH = dot(bsdf_context.mN, bsdf_context.mH);
		bsdf_context.mHdotV = dot(bsdf_context.mH, bsdf_context.mV);
		bsdf_context.mHdotL = dot(bsdf_context.mH, bsdf_context.mL);

		bsdf_context.mBSDF = 0;
		bsdf_context.mBSDFPDF = 0;

		bsdf_context.mEta = 1;

		return bsdf_context;
	}

	struct Source
	{
		static float3 Albedo(HitContext inHitContext)
		{
			uint albedo_texture_index = InstanceDatas[inHitContext.mInstanceID].mAlbedoTextureIndex;
			[branch]
			if (albedo_texture_index != (uint)ViewDescriptorIndex::Invalid)
			{
				Texture2D<float4> albedo_texture = ResourceDescriptorHeap[albedo_texture_index];
				return albedo_texture.SampleLevel(BilinearWrapSampler, inHitContext.mUV, 0).rgb;
			}

			return InstanceDatas[inHitContext.mInstanceID].mAlbedo;
		}
	};

	namespace Distribution
	{
		namespace GGX
		{
			float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
			{
				float a = InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha;
				float a2 = a * a;

				float3 H; // Microfacet normal (Half-vector)
				{
					float e0 = RandomFloat01(ioPathContext.mRandomState);
					float e1 = RandomFloat01(ioPathContext.mRandomState);

					// 2D Distribution -> GGX Distribution (Polar)
					float cos_theta = safe_sqrt((1.0 - e0) / ((a2 - 1) * e0 + 1.0));
					float sin_theta = safe_sqrt(1 - cos_theta * cos_theta);
					float phi = 2 * MATH_PI * e1;

					// Polar -> Cartesian
					H.x = sin_theta * cos(phi);
					H.y = sin_theta * sin(phi);
					H.z = cos_theta;

					// Tangent -> World
					H = normalize(H.x * inTangentSpace[0] + H.y * inTangentSpace[1] + H.z * inTangentSpace[2]);
				}

				// Assume N, H, V on the same side
				float3 V = dot(inTangentSpace[2], inHitContext.mRayDirectionWS) < 0 ? -inHitContext.mRayDirectionWS : inHitContext.mRayDirectionWS;
				float HdotV = dot(H, V);
				return 2.0 * HdotV * H - V;
			}
		}
	}

	namespace Lambert
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 direction = RandomCosineDirection(ioPathContext.mRandomState);
			return normalize(direction.x * inTangentSpace[0] + direction.y * inTangentSpace[1] + direction.z * inTangentSpace[2]);
		}

		void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			ioBSDFContext.mNdotL = max(0, ioBSDFContext.mNdotL);

			ioBSDFContext.mBSDF = BSDFEvaluation::Source::Albedo(inHitContext) / MATH_PI;
			ioBSDFContext.mBSDFPDF = ioBSDFContext.mNdotL / MATH_PI;
		}
	};

	namespace RoughConductor
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			return Distribution::GGX::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		}

		void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			float D = D_GGX(ioBSDFContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float G = G_SmithGGX(ioBSDFContext.mNdotL, ioBSDFContext.mNdotV, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float3 F = F_Conductor_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta, InstanceDatas[inHitContext.mInstanceID].mK, ioBSDFContext.mHdotV) * InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance;

			// [NOTE] Visible normal not supported yet
			//        [Mitsuba3] use visible normal sampling by default which affects both BSDF and BSDFPDF
			//        [TODO] Visible normal sampling not compatible with height-correlated visibility term?

			if (D < 0 || ioBSDFContext.mNdotL < 0 || ioBSDFContext.mNdotV < 0)
				D = 0;

			// [NOTE] Sample may return BSDF * NdotL / PDF as a whole or in separated terms, which varies between implementations.
			//        Also, NdotL needs to be eliminated for Dirac delta distribution.
			//        [PBRT3] `Sample_f` return BSDF and PDF. Dirac delta distribution (e.g. BSDF_SPECULAR) divide an extra NdotL for BSDF. https://github.com/mmp/pbrt-v3/blob/aaa552a4b9cbf9dccb71450f47b268e0ed6370e2/src/core/reflection.cpp#L410
			//        [Mitsuba3] `sample` returns as whole. Dirac delta distribution (e.g. ) does not have the NdotL term. https://github.com/mitsuba-renderer/mitsuba3/blob/master/src/bsdfs/roughconductor.cpp#L226
			//		         [NOTE] In `sample`, assume m_sample_visible = false,
			//                      BSDF * NdotL / PDF = F * G * ioBSDFContext.mHdotV / (ioBSDFContext.mNdotV * ioBSDFContext.mNdotH) * (D * ioBSDFContext.mNdotH) / (4.0f * ioBSDFContext.mHdotL)
			//			            HdotV and HdotL does not seems to cancel out at first glance.
			//                      The thing is, H is the half vector, HdotV == HdotL. (May differs due to floating point arithmetic though)
			//                      [TODO] Where does the HdotV come from in the first place?

			// [NOTE] Naming of wi/wo varies between implementations depending on point of view. The result should be same.
			//		  [PBRT3] has wo as V, wi as L
			// 		  [Mitsuba3] has wi as V, wo as L
			//        https://www.shadertoy.com/view/MsXfz4 has wi as V, wo as L

			ioBSDFContext.mBSDF = D * G * F / (4.0f * ioBSDFContext.mNdotV * ioBSDFContext.mNdotL);
			ioBSDFContext.mBSDFPDF	= (D * ioBSDFContext.mNdotH) / (4.0f * ioBSDFContext.mHdotL); /* mHdotL == mHdotV */

			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(F, 0));
			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mHdotV, ioBSDFContext.mHdotL, 0, 0));
 			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mNdotL, ioBSDFContext.mNdotV, 0, ioBSDFContext.mNdotH));
			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(D, ioBSDFContext.mBSDFPDF, 0, 0));
		}
	}

	namespace Dielectric
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			// Dummy, will generate sampling direction in Evaluate
			return reflect(inHitContext.mRayDirectionWS, inTangentSpace[2]);
		}

		void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			// [NOTE] L and related information are not avaiable yet before selection between reflection and refraction

			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			F_Dielectric_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta.x, ioBSDFContext.mNdotV, r_i, cos_theta_t, eta_it, eta_ti);

			if (GetBSDF(inHitContext) == BSDF::ThinDielectric)
			{
				// Account for internal reflections: r' = r + trt + tr^3t + ..
				// [NOTE] r' = r + trt + tr^3t + .. 
				//           = r + (1 - r) * r * (1-r) + (1-r) * r^3 * (1-r) + ...
				//			 = r + (1 - r) ^ 2 * r * \sum_0^\infty r^{2i}
				//			 = r * 2 / (r+1)
				r_i *= 2.0f / (1.0f + r_i);

				// No change of direction for transmittion
				cos_theta_t = ioBSDFContext.mNdotV;
				eta_it = 1.0f;
				eta_ti = 1.0f;
			}

			bool selected_r = RandomFloat01(ioPathContext.mRandomState) <= r_i;
			float3 L = select(selected_r,
				reflect(-ioBSDFContext.mV, ioBSDFContext.mNdotV < 0 ? -ioBSDFContext.mN : ioBSDFContext.mN),
				refract(-ioBSDFContext.mV, ioBSDFContext.mNdotV < 0 ? -ioBSDFContext.mN : ioBSDFContext.mN, eta_ti));

			ioBSDFContext = GenerateContext(ioBSDFContext.mMode, L, ioBSDFContext.mN, ioBSDFContext.mV, select(selected_r, 1.0, eta_it), inHitContext);

			DebugValue(PixelDebugMode::L2, ioPathContext.mRecursionCount, float4(ioBSDFContext.mL, 0));
			DebugValue(PixelDebugMode::V2, ioPathContext.mRecursionCount, float4(ioBSDFContext.mV, 0));
			DebugValue(PixelDebugMode::N2, ioPathContext.mRecursionCount, float4(ioBSDFContext.mN, 0));
			DebugValue(PixelDebugMode::H2, ioPathContext.mRecursionCount, float4(ioBSDFContext.mH, 0));
			DebugValue(PixelDebugMode::BSDF_Material, ioPathContext.mRecursionCount, float4(0, 0, 0, 0));

			ioBSDFContext.mBSDF = select(selected_r, InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance, InstanceDatas[inHitContext.mInstanceID].mSpecularTransmittance) * select(selected_r, r_i, 1.0 - r_i);
			ioBSDFContext.mBSDFPDF = select(selected_r, r_i, 1.0 - r_i);

			// [NOTE] Account for solid angle compression
			//        [Mitsuba3] > For transmission, radiance must be scaled to account for the solid angle compression that occurs when crossing the interface. 
			//                   https://github.com/mitsuba-renderer/mitsuba3/blob/master/src/bsdfs/dielectric.cpp#L359
			//        [PBRT3] > Account for non-symmetry with transmission to different medium 
			//	              https://github.com/mmp/pbrt-v3/blob/aaa552a4b9cbf9dccb71450f47b268e0ed6370e2/src/core/reflection.cpp#L163
			//		          https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/The_Path-Space_Measurement_Equation#x3-Non-symmetryDuetoRefraction
			ioBSDFContext.mBSDF *= select(selected_r, 1.0, sqr(eta_ti));

			// [NOTE] Output eta (inverse) to remove its effect on Russian Roulette. As Russian Roulette depends on throughput, which in turn depends on BSDF.
			//        The difference is easily observable when Russian Roulette is enabled even for the first iterations.
			//		  [PBRT3] > It lets us sometimes avoid terminating refracted rays that are about to be refracted back out of a medium and thus have their beta value increased.
			//                https://github.com/mmp/pbrt-v3/blob/master/src/integrators/path.cpp#L72
			ioBSDFContext.mEta = select(selected_r, 1.0, eta_it);

			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(-ioBSDFContext.mV, selected_r));
			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(L, 0));
		}
	}

	namespace RoughDielectric
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			return Distribution::GGX::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		}

		void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			F_Dielectric_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta.x, ioBSDFContext.mNdotV, r_i, cos_theta_t, eta_it, eta_ti);
	
			bool selected_r = false;
			if (ioBSDFContext.mMode == BSDFContext::Mode::BSDFSample)
			{
				selected_r = RandomFloat01(ioPathContext.mRandomState) <= r_i;

				float3 L = select(selected_r,
					reflect(-ioBSDFContext.mV, ioBSDFContext.mNdotV < 0 ? -ioBSDFContext.mH : ioBSDFContext.mH),
					refract(-ioBSDFContext.mV, ioBSDFContext.mNdotV < 0 ? -ioBSDFContext.mH : ioBSDFContext.mH, eta_ti));

				ioBSDFContext = GenerateContext(ioBSDFContext.mMode, L, ioBSDFContext.mN, ioBSDFContext.mV, select(selected_r, 1.0, eta_it), inHitContext);

				DebugValue(PixelDebugMode::L2, ioPathContext.mRecursionCount, float4(ioBSDFContext.mL, r_i));
				DebugValue(PixelDebugMode::V2, ioPathContext.mRecursionCount, float4(ioBSDFContext.mV, cos_theta_t));
				DebugValue(PixelDebugMode::N2, ioPathContext.mRecursionCount, float4(ioBSDFContext.mN, selected_r));
				DebugValue(PixelDebugMode::H2, ioPathContext.mRecursionCount, float4(ioBSDFContext.mH, ioBSDFContext.mNdotH));
				DebugValue(PixelDebugMode::BSDF_Material, ioPathContext.mRecursionCount, float4(0, 0, 0, 0));
			}
			else
			{
				selected_r = ioBSDFContext.mNdotV * ioBSDFContext.mNdotL > 0;

				ioBSDFContext = GenerateContext(ioBSDFContext.mMode, ioBSDFContext.mL, ioBSDFContext.mN, ioBSDFContext.mV, select(selected_r, 1.0, eta_it), inHitContext);

				DebugValue(PixelDebugMode::L3, ioPathContext.mRecursionCount, float4(ioBSDFContext.mL, r_i));
				DebugValue(PixelDebugMode::V3, ioPathContext.mRecursionCount, float4(ioBSDFContext.mV, cos_theta_t));
				DebugValue(PixelDebugMode::N3, ioPathContext.mRecursionCount, float4(ioBSDFContext.mN, selected_r));
				DebugValue(PixelDebugMode::H3, ioPathContext.mRecursionCount, float4(ioBSDFContext.mH, ioBSDFContext.mNdotH));
				DebugValue(PixelDebugMode::Light_Material, ioPathContext.mRecursionCount, float4(0, 0, 0, 0));
			}

			float D = D_GGX(ioBSDFContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float G = G_SmithGGX(abs(ioBSDFContext.mNdotL), abs(ioBSDFContext.mNdotV), InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float3 F = select(selected_r, InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance, InstanceDatas[inHitContext.mInstanceID].mSpecularTransmittance);

			if (ioBSDFContext.mMode == BSDFContext::Mode::BSDFSample)
			{
				DebugValue(PixelDebugMode::BSDF_D, ioPathContext.mRecursionCount, float4(D, ioBSDFContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha, 0));
				DebugValue(PixelDebugMode::BSDF_G, ioPathContext.mRecursionCount, float4(G, 0, 0, 0));
				DebugValue(PixelDebugMode::BSDF_F, ioPathContext.mRecursionCount, float4(F, 0));
			}
			else
			{
				DebugValue(PixelDebugMode::Light_D, ioPathContext.mRecursionCount, float4(D, ioBSDFContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha, 0));
				DebugValue(PixelDebugMode::Light_G, ioPathContext.mRecursionCount, float4(G, 0, 0, 0));
				DebugValue(PixelDebugMode::Light_F, ioPathContext.mRecursionCount, float4(F, 0));
			}

			ioBSDFContext.mBSDF = abs(select(selected_r, 
				D * G * F / (4.0f * ioBSDFContext.mNdotV * ioBSDFContext.mNdotL),
				D * G * F * ioBSDFContext.mHdotV * (sqr(eta_it) * ioBSDFContext.mHdotL) / (ioBSDFContext.mNdotV * ioBSDFContext.mNdotL * sqr(ioBSDFContext.mHdotV + eta_it * ioBSDFContext.mHdotL))));
			ioBSDFContext.mBSDF *= select(selected_r, r_i, 1.0 - r_i);

			ioBSDFContext.mBSDFPDF = abs(select(selected_r, 
				(D * ioBSDFContext.mNdotH) / (4.0f * ioBSDFContext.mHdotL),
				(D * ioBSDFContext.mNdotH) * (sqr(eta_it) * ioBSDFContext.mHdotL) / sqr(ioBSDFContext.mHdotV + eta_it * ioBSDFContext.mHdotL)));
			ioBSDFContext.mBSDFPDF *= select(selected_r, r_i, 1.0 - r_i);

			// See Dielectric::Evaluate
			ioBSDFContext.mBSDF *= select(selected_r, 1.0, sqr(eta_ti));
			ioBSDFContext.mEta = select(selected_r, 1.0, eta_it);
		}
	}

	namespace DebugEmissive
	{
		void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			ioBSDFContext.mBSDF = 0.0f;
			ioBSDFContext.mBSDFPDF = 0.0f;
		}
	}

	namespace DebugMirror
	{
		void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			ioBSDFContext = GenerateContext(BSDFContext::Mode::LightSample, reflect(-ioBSDFContext.mV, ioBSDFContext.mN), ioBSDFContext.mN, ioBSDFContext.mV, 1.0, inHitContext);
			ioBSDFContext.mBSDF = 1.0f;
			ioBSDFContext.mBSDFPDF = 1.0f;
		}
	}

	float3 GenerateImportanceSamplingDirection(float3 inNormal, HitContext inHitContext, inout PathContext ioPathContext)
	{
		// [TODO] Refactor this as sample micro distribution and return H

		// Handle TwoSided
		if (dot(inNormal, -inHitContext.mRayDirectionWS) < 0 && InstanceDatas[inHitContext.mInstanceID].mTwoSided)
			inNormal = -inNormal;

		float3x3 inTangentSpace = GenerateTangentSpace(inNormal);
		[branch]
		switch (GetBSDF(inHitContext))
		{
		case BSDF::Unsupported:			// [[fallthrough]];
		case BSDF::Diffuse:				return Lambert::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDF::RoughConductor:		return RoughConductor::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDF::Dielectric:			return Dielectric::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDF::ThinDielectric:		return Dielectric::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDF::RoughDielectric:		return RoughDielectric::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		default:						return 0;
		}
	}

	BSDFContext GenerateImportanceSamplingContext(float3 inNormal, float3 inView, HitContext inHitContext, inout PathContext ioPathContext)
	{
		float3 importance_sampling_direction = GenerateImportanceSamplingDirection(inNormal, inHitContext, ioPathContext);

		// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(importance_sampling_direction, 0));

 		BSDFContext bsdf_context = BSDFEvaluation::GenerateContext(BSDFContext::Mode::BSDFSample, importance_sampling_direction, inHitContext.mVertexNormalWS, -inHitContext.mRayDirectionWS, 1.0, inHitContext);
		return bsdf_context;
	}

	void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
	{
		if (ioBSDFContext.mMode == BSDFContext::Mode::BSDFSample)
		{
			DebugValue(PixelDebugMode::L0, ioPathContext.mRecursionCount, float4(ioBSDFContext.mL, 0));
			DebugValue(PixelDebugMode::V0, ioPathContext.mRecursionCount, float4(ioBSDFContext.mV, 0));
			DebugValue(PixelDebugMode::N0, ioPathContext.mRecursionCount, float4(ioBSDFContext.mN, 0));
			DebugValue(PixelDebugMode::H0, ioPathContext.mRecursionCount, float4(ioBSDFContext.mH, 0));
			DebugValue(PixelDebugMode::BSDF_Importance_Sampling, ioPathContext.mRecursionCount, float4(0, 0, 0, 0));
		}
		else
		{
			DebugValue(PixelDebugMode::L1, ioPathContext.mRecursionCount, float4(ioBSDFContext.mL, 0));
			DebugValue(PixelDebugMode::V1, ioPathContext.mRecursionCount, float4(ioBSDFContext.mV, 0));
			DebugValue(PixelDebugMode::N1, ioPathContext.mRecursionCount, float4(ioBSDFContext.mN, 0));
			DebugValue(PixelDebugMode::H1, ioPathContext.mRecursionCount, float4(ioBSDFContext.mH, 0));
			DebugValue(PixelDebugMode::Light_Importance_Sampling, ioPathContext.mRecursionCount, float4(0, 0, 0, 0));
		}

		[branch]
		switch (GetBSDF(inHitContext))
		{
		case BSDF::Diffuse:				Lambert::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDF::RoughConductor:		RoughConductor::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDF::Dielectric:			Dielectric::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDF::ThinDielectric:		Dielectric::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDF::RoughDielectric:		RoughDielectric::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDF::DebugEmissive:		DebugEmissive::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDF::DebugMirror:			DebugMirror::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDF::Unsupported:			// [[fallthrough]];
		default:						break;
		}

		if (DiracDeltaDistribution(inHitContext))
		{
			// [NOTE] For Dirac delta distribution, the cosine term is defined to be canceled out with the one in integration
			// Considering it as part of integral to describe illuminance sounds okay, but still not very thorough... 
			// https://www.pbr-book.org/3ed-2018/Reflection_Models/Specular_Reflection_and_Transmission
			// https://stackoverflow.com/questions/22431912/path-tracing-why-is-there-no-cosine-term-when-calculating-perfect-mirror-reflec
			// https://gamedev.net/forums/topic/657520-cosine-term-in-rendering-equation/5159311/?page=2
			ioBSDFContext.mBSDF /= abs(ioBSDFContext.mNdotL);
		}

		// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mBSDF, ioBSDFContext.mBSDFPDF));
	}
}