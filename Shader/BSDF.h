#pragma once
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Context.h"

struct BSDFResult
{
	float3			mBSDF;
	float			mBSDFSamplePDF;
	float			mEta;
};

namespace BSDFEvaluation
{
	//	Interface
	// 
	//		DXRPlayground
	//			[TODO]
	// 
	//		Mistuba
	//			sample:							{ direction, pdf, bsdf * NdotL } Direction is the importance sample direction
	//			eval:							{ bsdf * NdotL} NdotL is the cosine foreshortening term
	//			pdf:							{ pdf } pdf of sampling at given direction
	//			eval_pdf:						{ eval, pdf }
	//			eval_pdf_sample:				{ eval_pdf, sample}
	//

	namespace Distribution
	{
		namespace GGX
		{
			float3 GenerateMicrofacetDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
			{
				float a							= inHitContext.RoughnessAlpha();
				float a2						= a * a;

				float3 H; // Microfacet normal (Half-vector), sometimes called m
				{
					float e0					= RandomFloat01(ioPathContext.mRandomState);
					float e1					= RandomFloat01(ioPathContext.mRandomState);

					// 2D Distribution -> GGX Distribution (Polar)
					float cos_theta				= safe_sqrt((1.0 - e0) / ((a2 - 1) * e0 + 1.0));
					float sin_theta				= safe_sqrt(1 - cos_theta * cos_theta);
					float phi					= 2 * MATH_PI * e1;

					// Polar -> Cartesian
					H.x							= sin_theta * cos(phi);
					H.y							= sin_theta * sin(phi);
					H.z							= cos_theta;

					// Tangent -> World
					H							= normalize(H.x * inTangentSpace[0] + H.y * inTangentSpace[1] + H.z * inTangentSpace[2]);
				}

				return H;
			}
		}
	}

	namespace Diffuse
	{
		BSDFContext GenerateContext(HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3x3 tangent_space				= GenerateTangentSpace(inHitContext.NormalWS());
			float3 randome_direction			= RandomCosineDirection(ioPathContext.mRandomState);
			float3 L							= normalize(randome_direction.x * tangent_space[0] + randome_direction.y * tangent_space[1] + randome_direction.z * tangent_space[2]);

			return BSDFContext::Generate(BSDFContext::Mode::BSDF, L, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			BSDFResult result;
			result.mBSDF						= inHitContext.Albedo() / MATH_PI;
			result.mBSDFSamplePDF				= max(0, inBSDFContext.mNdotL) / MATH_PI;
			result.mEta							= 1.0;

			if (inHitContext.BSDF() == BSDF::Unsupported)
				result.mBSDF					= float3(1, 0, 1) / MATH_PI;

			if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0 || inBSDFContext.mHdotL < 0 || inBSDFContext.mHdotV < 0)
				result.mBSDF					= 0;

			if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
				result.mBSDF					= 0.0;

			return result;
		}
	};

	namespace Conductor
	{
		BSDFContext GenerateContext(HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 L							= reflect(-inHitContext.ViewWS(), inHitContext.NormalWS());

			return BSDFContext::Generate(BSDFContext::Mode::BSDF, L, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 F							= F_Conductor_Mitsuba(inHitContext.Eta(), inHitContext.K(), inBSDFContext.mHdotV) * inHitContext.SpecularReflectance();

			if (inBSDFContext.mMode == BSDFContext::Mode::BSDF)
			{
				DebugValue(DebugMode::BSDF__D, ioPathContext.mRecursionDepth, float3(qnan(), 0, 0));
				DebugValue(DebugMode::BSDF__G, ioPathContext.mRecursionDepth, float3(qnan(), 0, 0));
				DebugValue(DebugMode::BSDF__F, ioPathContext.mRecursionDepth, float3(F));
			}
			else
			{
				DebugValue(DebugMode::Light_D, ioPathContext.mRecursionDepth, float3(qnan(), 0, 0));
				DebugValue(DebugMode::Light_G, ioPathContext.mRecursionDepth, float3(qnan(), 0, 0));
				DebugValue(DebugMode::Light_F, ioPathContext.mRecursionDepth, float3(F));
			}

			BSDFResult result;
			result.mBSDF						= F;
			result.mBSDFSamplePDF				= 1.0;
			result.mEta							= 1.0;

			if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0 || inBSDFContext.mHdotL < 0 || inBSDFContext.mHdotV < 0)
				result.mBSDF					= 0;

			if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Reflection)
				result.mBSDF					= 1.0;

			return result;
		}
	}

	namespace RoughConductor
	{
		BSDFContext GenerateContext(HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3x3 tangent_space				= GenerateTangentSpace(inHitContext.NormalWS());
			float3 H							= Distribution::GGX::GenerateMicrofacetDirection(tangent_space, inHitContext, ioPathContext);
			float3 V							= inHitContext.ViewWS();
			float HdotV							= dot(H, V);
			float3 L							= 2.0 * HdotV * H - V;

			return BSDFContext::Generate(BSDFContext::Mode::BSDF, L, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			// [TODO] Use Smith's separable shadowing-masking approximation. Ensure consistent orientation in G

			float D								= D_GGX(inBSDFContext.mNdotH, inHitContext.RoughnessAlpha());
			float G								= G_SmithGGX(inBSDFContext.mNdotL, inBSDFContext.mNdotV, inHitContext.RoughnessAlpha());
			float3 F							= F_Conductor_Mitsuba(inHitContext.Eta(), inHitContext.K(), inBSDFContext.mHdotV) * inHitContext.SpecularReflectance();

			// [NOTE] Visible normal not supported yet
			//        [Mitsuba3] use visible normal sampling by default which affects both BSDF and BSDFPDF
			//        [TODO] Visible normal sampling not compatible with height-correlated visibility term?

			if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0 || inBSDFContext.mHdotL < 0 || inBSDFContext.mHdotV < 0)
				D								= 0;

			if (inBSDFContext.mMode == BSDFContext::Mode::BSDF)
			{
				DebugValue(DebugMode::BSDF__D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
				DebugValue(DebugMode::BSDF__G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
				DebugValue(DebugMode::BSDF__F, ioPathContext.mRecursionDepth, float3(F));
			}
			else
			{
				DebugValue(DebugMode::Light_D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
				DebugValue(DebugMode::Light_G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
				DebugValue(DebugMode::Light_F, ioPathContext.mRecursionDepth, float3(F));
			}

			// [NOTE] Eval/Sample functions may return BSDF * NdotL / PDF as a whole or in separated terms, which varies between implementations.
			//        Also, NdotL needs to be eliminated for Dirac delta distribution.
			//		  [DXRPlayground] `Evaluate` return BSDF and PDF. Dirac delta distribution (e.g. Conductor) divide an extra NdotL.
			//        [PBRT3] `Sample_f` return BSDF and PDF. Dirac delta distribution (e.g. BSDF_SPECULAR) divide an extra NdotL. https://github.com/mmp/pbrt-v3/blob/aaa552a4b9cbf9dccb71450f47b268e0ed6370e2/src/core/reflection.cpp#L410
			//        [Mitsuba3] `sample` returns as BSDF * NdotL / PDF and PDF, note HdotV == HdotL. Dirac delta distribution (e.g. ) does not have the NdotL term. https://github.com/mitsuba-renderer/mitsuba3/blob/master/src/bsdfs/roughconductor.cpp#L226

			// [NOTE] Naming of wi/wo varies between implementations depending on point of view. The result should be same.
			//		  [PBRT3] has wo as V, wi as L
			// 		  [Mitsuba3] has wi as V, wo as L
			//        https://www.shadertoy.com/view/MsXfz4 has wi as V, wo as L

			float microfacet_pdf				= D * inBSDFContext.mNdotH;
			float jacobian						= 1.0 / (4.0f * inBSDFContext.mHdotL);

			BSDFResult result;
			result.mBSDF						= D * G * F / (4.0f * inBSDFContext.mNdotV * inBSDFContext.mNdotL);
			result.mBSDFSamplePDF				= microfacet_pdf * jacobian;
			result.mEta							= 1.0;
			return result;
		}
	}

	namespace Dielectric
	{
		void PatchThinDielectricBefore(HitContext inHitContext, inout float ioCosTheta, inout float ioEta)
		{
			if (inHitContext.BSDF() != BSDF::ThinDielectric)
				return;

			// [NOTE] ThinDielectric essentially mean IOR is same on both side, hence the abs
			ioCosTheta = abs(ioCosTheta);
		}

		void PatchThinDielectricAfter(HitContext inHitContext, inout float ioR, inout float ioCosThetaT, inout float ioEtaIT, inout float ioEtaTI)
		{
			if (inHitContext.BSDF() != BSDF::ThinDielectric)
				return;

			// Account for internal reflections: r' = r + trt + tr^3t + ..
			// [NOTE] r' = r + trt + tr^3t + .. 
			//           = r + (1 - r) * r * (1-r) + (1-r) * r^3 * (1-r) + ...
			//			 = r + (1 - r) ^ 2 * r * \sum_0^\infty r^{2i}
			//			 = r * 2 / (r+1)
			ioR									*= 2.0f / (1.0f + ioR);
			// Need to patch cos_theta_t either, but leave it since unused
			ioEtaIT								= 1.0f;
			ioEtaTI								= 1.0f;

			// [NOTE] Somehow in implementation of Mitsuba3, flag DeltaTransmission looks missing, not sure why
			// https://github.com/mitsuba-renderer/mitsuba/blob/10af06f365886c1b6dd8818e0a3841078a62f283/src/bsdfs/thindielectric.cpp#L226
		}

		BSDFContext GenerateContext(HitContext inHitContext, inout PathContext ioPathContext)
		{
			float cos_theta						= inHitContext.NdotV();
			float eta							= inHitContext.Eta().x;
			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			PatchThinDielectricBefore(inHitContext, cos_theta, eta);
			F_Dielectric_Mitsuba(cos_theta, eta, r_i, cos_theta_t, eta_it, eta_ti);
			PatchThinDielectricAfter(inHitContext, r_i, cos_theta_t, eta_it, eta_ti);

			bool selected_r						= RandomFloat01(ioPathContext.mRandomState) <= r_i;
			float3 L							= select(selected_r,
													reflect(-inHitContext.ViewWS(), inHitContext.NdotV() < 0 ? -inHitContext.NormalWS() : inHitContext.NormalWS()),
													refract(-inHitContext.ViewWS(), inHitContext.NdotV() < 0 ? -inHitContext.NormalWS() : inHitContext.NormalWS(), eta_ti));
			
			return BSDFContext::Generate(BSDFContext::Mode::BSDF, L, select(selected_r, 1.0, eta_it), selected_r, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float cos_theta						= inHitContext.NdotV();
			float eta							= inHitContext.Eta().x;
			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			PatchThinDielectricBefore(inHitContext, cos_theta, eta);
			F_Dielectric_Mitsuba(cos_theta, eta, r_i, cos_theta_t, eta_it, eta_ti);
			PatchThinDielectricAfter(inHitContext, r_i, cos_theta_t, eta_it, eta_ti);

			bool selected_r						= inBSDFContext.mLobe0Selected;

			BSDFResult result;
			result.mBSDF						= select(selected_r, r_i, 1.0 - r_i) * select(selected_r, inHitContext.SpecularReflectance(), inHitContext.SpecularTransmittance());
			result.mBSDFSamplePDF				= select(selected_r, r_i, 1.0 - r_i);

			// [NOTE] Account for solid angle compression
			//        [Mitsuba3] > For transmission, radiance must be scaled to account for the solid angle compression that occurs when crossing the interface. 
			//                   https://github.com/mitsuba-renderer/mitsuba3/blob/master/src/bsdfs/dielectric.cpp#L359
			//        [PBRT3] > Account for non-symmetry with transmission to different medium 
			//	              https://github.com/mmp/pbrt-v3/blob/aaa552a4b9cbf9dccb71450f47b268e0ed6370e2/src/core/reflection.cpp#L163
			//		          https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/The_Path-Space_Measurement_Equation#x3-Non-symmetryDuetoRefraction
			result.mBSDF						*= select(selected_r, 1.0, sqr(eta_ti));

			// [NOTE] Output eta (inverse) to remove its effect on Russian Roulette. As Russian Roulette depends on throughput, which in turn depends on BSDF.
			//        The difference is easily observable when Russian Roulette is enabled even for the first iterations.
			//		  [PBRT3] > It lets us sometimes avoid terminating refracted rays that are about to be refracted back out of a medium and thus have their beta value increased.
			//                https://github.com/mmp/pbrt-v3/blob/master/src/integrators/path.cpp#L72
			result.mEta							= select(selected_r, 1.0, eta_it);

			return result;
		}
	}

	namespace RoughDielectric
	{
		BSDFContext GenerateContext(HitContext inHitContext, inout PathContext ioPathContext)
		{
			float cos_theta						= inHitContext.NdotV();
			float eta							= inHitContext.Eta().x;
			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			F_Dielectric_Mitsuba(cos_theta, eta, r_i, cos_theta_t, eta_it, eta_ti);

			float3x3 tangent_space				= GenerateTangentSpace(inHitContext.NormalWS());
			float3 H							= Distribution::GGX::GenerateMicrofacetDirection(tangent_space, inHitContext, ioPathContext);
			float3 V							= inHitContext.ViewWS();

			bool selected_r = RandomFloat01(ioPathContext.mRandomState) <= r_i;
			float3 L = select(selected_r,
				reflect(-inHitContext.ViewWS(), inHitContext.NdotV() < 0 ? -H : H),
				refract(-inHitContext.ViewWS(), inHitContext.NdotV() < 0 ? -H : H, eta_ti));

			return BSDFContext::Generate(BSDFContext::Mode::BSDF, L, select(selected_r, 1.0, eta_it), selected_r, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			BSDFResult result					= (BSDFResult)0;

			float cos_theta						= inHitContext.NdotV();
			float eta							= inHitContext.Eta().x;
			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			F_Dielectric_Mitsuba(cos_theta, eta, r_i, cos_theta_t, eta_it, eta_ti);

			bool selected_r						= inBSDFContext.mLobe0Selected;

			if (inBSDFContext.mMode == BSDFContext::Mode::BSDF)
				DebugValue(DebugMode::BSDF__I, ioPathContext.mRecursionDepth, float3(selected_r ? 0 : 1, 0, 0));
			else
				DebugValue(DebugMode::Light_I, ioPathContext.mRecursionDepth, float3(selected_r ? 0 : 1, 0, 0));

			if (selected_r)
			{
				// Effectively TwoSided for reflection
				if (inBSDFContext.mNdotV < 0)
					inBSDFContext.FlipNormal();

				// See RoughConductor::Evaluate

				float D							= D_GGX(inBSDFContext.mNdotH, inHitContext.RoughnessAlpha());
				float G							= G_SmithGGX(inBSDFContext.mNdotL, inBSDFContext.mNdotV, inHitContext.RoughnessAlpha());
				float3 F						= r_i * inHitContext.SpecularReflectance();

				if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0 || inBSDFContext.mHdotL < 0 || inBSDFContext.mHdotV < 0)
					D							= 0;

				if (inBSDFContext.mMode == BSDFContext::Mode::BSDF)
				{
					DebugValue(DebugMode::BSDF__D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
					DebugValue(DebugMode::BSDF__G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
					DebugValue(DebugMode::BSDF__F, ioPathContext.mRecursionDepth, float3(F));
				}
				else
				{
					DebugValue(DebugMode::Light_D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
					DebugValue(DebugMode::Light_G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
					DebugValue(DebugMode::Light_F, ioPathContext.mRecursionDepth, float3(F));
				}

				float microfacet_pdf			= D * inBSDFContext.mNdotH;
				float jacobian					= 1.0 / (4.0f * inBSDFContext.mHdotL);

				result.mBSDF					= D * G * F / (4.0f * inBSDFContext.mNdotV * inBSDFContext.mNdotL);
				result.mBSDFSamplePDF			= microfacet_pdf * jacobian;
			}
			else
			{
				// Based on RoughDieletric in Mitsuba, which is an implementation of [WMLT07] Microfacet Models for Refraction through Rough Surfaces
				// Omit roughness scale for now

				// Patch eta
				if (inBSDFContext.mMode == BSDFContext::Mode::Light)
					inBSDFContext.SetEta(eta_it);

				float D							= D_GGX(inBSDFContext.mNdotH, inHitContext.RoughnessAlpha());
				float G							= G_SmithGGX(inBSDFContext.mNdotL, inBSDFContext.mNdotV, inHitContext.RoughnessAlpha());
				float3 F						= (1.0 - r_i) * inHitContext.SpecularTransmittance();

				if (inBSDFContext.mMode == BSDFContext::Mode::BSDF)
				{
					DebugValue(DebugMode::BSDF__D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
					DebugValue(DebugMode::BSDF__G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
					DebugValue(DebugMode::BSDF__F, ioPathContext.mRecursionDepth, float3(F));
				}
				else
				{
					DebugValue(DebugMode::Light_D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
					DebugValue(DebugMode::Light_G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
					DebugValue(DebugMode::Light_F, ioPathContext.mRecursionDepth, float3(F));
				}

				float microfacet_pdf			= D * inBSDFContext.mNdotH;
				float jacobian					= abs(sqr(eta_it) * inBSDFContext.mHdotL / sqr(inBSDFContext.mHdotV + eta_it * inBSDFContext.mHdotL));

				result.mBSDF					= abs(D * G * F * inBSDFContext.mHdotV * jacobian / (abs(inBSDFContext.mNdotV) * abs(inBSDFContext.mNdotL)));
				result.mBSDFSamplePDF			= microfacet_pdf * jacobian;
			}
			result.mBSDFSamplePDF				*= select(selected_r, r_i, 1.0 - r_i);

			// See Dielectric::Evaluate
			result.mBSDF						*= select(selected_r, 1.0, sqr(eta_ti));
			result.mEta							= select(selected_r, 1.0, eta_it);

			return result;
		}
	}

	namespace glTF
	{
		// [TODO] Add GGX Specular to match RTXDI
		
		BSDFContext GenerateContext(HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3x3 tangent_space				= GenerateTangentSpace(inHitContext.NormalWS());
			float3 randome_direction			= RandomCosineDirection(ioPathContext.mRandomState);
			float3 L							= normalize(randome_direction.x * tangent_space[0] + randome_direction.y * tangent_space[1] + randome_direction.z * tangent_space[2]);

			return BSDFContext::Generate(BSDFContext::Mode::BSDF, L, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			BSDFResult result;
			result.mBSDF						= inHitContext.Albedo() / MATH_PI;
			result.mBSDFSamplePDF				= max(0, inBSDFContext.mNdotL) / MATH_PI;
			result.mEta							= 1.0;

			if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0 || inBSDFContext.mHdotL < 0 || inBSDFContext.mHdotV < 0)
				result.mBSDF					= 0;

			if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
				result.mBSDF					= 0.0;

			return result;
		}
	};

	BSDFContext GenerateContext(HitContext inHitContext, inout PathContext ioPathContext)
	{
		BSDFContext bsdf_context;

		switch (inHitContext.BSDF())
		{
		case BSDF::Unsupported:					bsdf_context = Diffuse::GenerateContext(inHitContext, ioPathContext); break;
		case BSDF::Diffuse:						bsdf_context = Diffuse::GenerateContext(inHitContext, ioPathContext); break;
		case BSDF::Conductor:					bsdf_context = Conductor::GenerateContext(inHitContext, ioPathContext); break;
		case BSDF::RoughConductor:				bsdf_context = RoughConductor::GenerateContext(inHitContext, ioPathContext); break;
		case BSDF::Dielectric:					bsdf_context = Dielectric::GenerateContext(inHitContext, ioPathContext); break;
		case BSDF::ThinDielectric:				bsdf_context = Dielectric::GenerateContext(inHitContext, ioPathContext); break;
		case BSDF::RoughDielectric:				bsdf_context = RoughDielectric::GenerateContext(inHitContext, ioPathContext); break;
		case BSDF::pbrMetallicRoughness:						bsdf_context = glTF::GenerateContext(inHitContext, ioPathContext); break;
		default:								bsdf_context = Diffuse::GenerateContext(inHitContext, ioPathContext); break;
		}

		DebugValue(DebugMode::BSDF__L,		ioPathContext.mRecursionDepth, float3(bsdf_context.mL));
		DebugValue(DebugMode::BSDF__V,		ioPathContext.mRecursionDepth, float3(bsdf_context.mV));
		DebugValue(DebugMode::BSDF__N,		ioPathContext.mRecursionDepth, float3(bsdf_context.mN));
		DebugValue(DebugMode::BSDF__H,		ioPathContext.mRecursionDepth, float3(bsdf_context.mH));

		return bsdf_context;
	}

	BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
	{
		BSDFResult result;
		switch (inHitContext.BSDF())
		{
		case BSDF::Diffuse:						result = Diffuse::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::Conductor:					result = Conductor::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::RoughConductor:				result = RoughConductor::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::Dielectric:					result = Dielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::ThinDielectric:				result = Dielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::RoughDielectric:				result = RoughDielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::pbrMetallicRoughness:						result = glTF::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::Unsupported:					result = Diffuse::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		default:								result = Diffuse::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		}

		if (inHitContext.DiracDeltaDistribution())
		{
			// [NOTE] For Dirac delta distribution, the cosine term is defined to be canceled out with the one in integration
			// Considering it as part of integral to describe illuminance sounds okay, but still not very thorough... 
			// https://www.pbr-book.org/3ed-2018/Reflection_Models/Specular_Reflection_and_Transmission
			// https://stackoverflow.com/questions/22431912/path-tracing-why-is-there-no-cosine-term-when-calculating-perfect-mirror-reflec
			// https://gamedev.net/forums/topic/657520-cosine-term-in-rendering-equation/5159311/?page=2
			result.mBSDF						/= abs(inBSDFContext.mNdotL);
		}

		return result;
	}
}