#pragma once
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Context.h"
#include "DebugUtils.h"
#include "BRDFExplorer.h"

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
					float cos_theta				= SafeSqrt((1.0 - e0) / ((a2 - 1) * e0 + 1.0));
					float sin_theta				= SafeSqrt(1 - cos_theta * cos_theta);
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
		BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inL, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 L = inL;
			if (inMode == BSDFContext::Mode::BSDF)
			{
				float3x3 tangent_space			= GenerateTangentSpace(inHitContext.NormalWS());
				float3 randome_direction		= RandomCosineDirection(ioPathContext.mRandomState);
				L								= normalize(randome_direction.x * tangent_space[0] + randome_direction.y * tangent_space[1] + randome_direction.z * tangent_space[2]);
			}

			return BSDFContext::Generate(inMode, L, ContextConstant::sEtaITTrivial, ContextConstant::sLobeIndexTrivial, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			BSDFResult result;
			result.mBSDF						= inHitContext.Albedo() / MATH_PI;
			result.mBSDFSamplePDF				= max(0, inBSDFContext.mNdotL) / MATH_PI;
			result.mEta							= 1.0;
			result.mMediumInstanceID			= InvalidInstanceID;

			if (inHitContext.BSDF() == BSDF::Unsupported)
				result.mBSDF					= float3(1, 0, 1) / MATH_PI;

			if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0)
				result.mBSDF					= 0;

			return result;
		}
	};

	namespace Conductor
	{
		BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inL, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 L							= inL;
			if (inMode == BSDFContext::Mode::BSDF)
			{
				L								= reflect(-inHitContext.ViewWS(), inHitContext.NormalWS());
			}

			return BSDFContext::Generate(inMode, L, ContextConstant::sEtaITTrivial, ContextConstant::sLobeIndexTrivial, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 F							= F_Conductor_Mitsuba(inHitContext.Eta(), inHitContext.K(), inBSDFContext.mHdotV) * inHitContext.SpecularReflectance();

			BSDFResult result;
			result.mBSDF						= F;
			result.mBSDFSamplePDF				= 1.0;
			result.mEta							= 1.0;
			result.mMediumInstanceID			= InvalidInstanceID;

			if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0 || inBSDFContext.mHdotL < 0 || inBSDFContext.mHdotV < 0)
				result.mBSDF					= 0;

			Inspect::DGF(ioPathContext, inBSDFContext, QNaN(), QNaN(), F);

			return result;
		}
	}

	namespace RoughConductor
	{
		BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inL, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 L = inL;
			if (inMode == BSDFContext::Mode::BSDF)
			{
				float3x3 tangent_space			= GenerateTangentSpace(inHitContext.NormalWS());
				float3 H						= Distribution::GGX::GenerateMicrofacetDirection(tangent_space, inHitContext, ioPathContext);
				float3 V						= inHitContext.ViewWS();
				float HdotV						= dot(H, V);
				L								= 2.0 * HdotV * H - V;
			}

			return BSDFContext::Generate(inMode, L, ContextConstant::sEtaITTrivial, ContextConstant::sLobeIndexTrivial, inHitContext);
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
			result.mMediumInstanceID			= InvalidInstanceID;

			Inspect::DGF(ioPathContext, inBSDFContext, D, G, F);

			return result;
		}
	}

	namespace Dielectric
	{
		static uint sLobeIndexReflection = 0;
		static uint sLobeIndexRefraction = 1;

		void PatchThinDielectricBefore(HitContext inHitContext, inout float ioCosTheta, inout float ioEta)
		{
			if (!USE_BSDF_ThinDielectric || inHitContext.BSDF() != BSDF::ThinDielectric)
				return;

			// [NOTE] ThinDielectric essentially mean IOR is same on both side, hence the abs
			ioCosTheta = abs(ioCosTheta);
		}

		void PatchThinDielectricAfter(HitContext inHitContext, inout float ioR, inout float ioCosThetaT, inout float ioEtaIT, inout float ioEtaTI)
		{
			if (!USE_BSDF_ThinDielectric || inHitContext.BSDF() != BSDF::ThinDielectric)
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

		BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inL, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float cos_theta						= inHitContext.NdotV();
			float eta							= inHitContext.Eta().x;
			float r_i, cos_theta_t, eta_it, eta_ti;
			PatchThinDielectricBefore(inHitContext, cos_theta, eta);
			F_Dielectric_Mitsuba(cos_theta, eta, r_i, cos_theta_t, eta_it, eta_ti);
			PatchThinDielectricAfter(inHitContext, r_i, cos_theta_t, eta_it, eta_ti);

			uint lobe_index						= inHitContext.NdotV() * dot(inHitContext.NormalWS(), inL) >= 0 ? sLobeIndexReflection : sLobeIndexRefraction;
			float3 L							= inL;
			if (inMode == BSDFContext::Mode::BSDF)
			{
				lobe_index						= RandomFloat01(ioPathContext.mRandomState) <= r_i ? sLobeIndexReflection : sLobeIndexRefraction;
				L								= select(lobe_index == sLobeIndexReflection,
													reflect(-inHitContext.ViewWS(), inHitContext.NdotV() < 0 ? -inHitContext.NormalWS() : inHitContext.NormalWS()),
													refract(-inHitContext.ViewWS(), inHitContext.NdotV() < 0 ? -inHitContext.NormalWS() : inHitContext.NormalWS(), eta_ti));
			}

			return BSDFContext::Generate(inMode, L, select(lobe_index == sLobeIndexReflection, 1.0, eta_it), lobe_index, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float cos_theta						= inHitContext.NdotV();
			float eta							= inHitContext.Eta().x;
			float r_i, cos_theta_t, eta_it, eta_ti;
			PatchThinDielectricBefore(inHitContext, cos_theta, eta);
			F_Dielectric_Mitsuba(cos_theta, eta, r_i, cos_theta_t, eta_it, eta_ti);
			PatchThinDielectricAfter(inHitContext, r_i, cos_theta_t, eta_it, eta_ti);

			bool select_reflection				= inBSDFContext.mLobeIndex == sLobeIndexReflection;

			BSDFResult result;
			result.mBSDF						= select(select_reflection, r_i, 1.0 - r_i) * select(select_reflection, inHitContext.SpecularReflectance(), inHitContext.SpecularTransmittance());
			result.mBSDFSamplePDF				= select(select_reflection, r_i, 1.0 - r_i);

			// [NOTE] Account for solid angle compression
			//        [Mitsuba3] > For transmission, radiance must be scaled to account for the solid angle compression that occurs when crossing the interface. 
			//                   https://github.com/mitsuba-renderer/mitsuba3/blob/master/src/bsdfs/dielectric.cpp#L359
			//        [PBRT3] > Account for non-symmetry with transmission to different medium 
			//	              https://github.com/mmp/pbrt-v3/blob/aaa552a4b9cbf9dccb71450f47b268e0ed6370e2/src/core/reflection.cpp#L163
			//		          https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/The_Path-Space_Measurement_Equation#x3-Non-symmetryDuetoRefraction
			result.mBSDF						*= select(select_reflection, 1.0, Sqr(eta_ti));

			// [NOTE] Output eta (inverse) to remove its effect on Russian Roulette. 
			//		  Russian Roulette terminates path early depending on throughput (beta), which in turn depending on BSDF above
			//		  Refraction in and out may temporarily lower throughput to cause termination unintentionally
			//		  [PBRT3] > It lets us sometimes avoid terminating refracted rays that are about to be refracted back out of a medium and thus have their beta value increased.
			//                https://github.com/mmp/pbrt-v3/blob/master/src/integrators/path.cpp#L72
			result.mEta							= select(select_reflection, 1.0, eta_it);
			result.mMediumInstanceID			= select(inHitContext.HasMedium() && inBSDFContext.mNdotL < 0, inHitContext.mInstanceID, InvalidInstanceID);

			return result;
		}
	}

	namespace RoughDielectric
	{
		static uint sLobeIndexReflection = 0;
		static uint sLobeIndexRefraction = 1;

		BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inL, HitContext inHitContext, inout PathContext ioPathContext)
		{
			// [TODO] eval+sample pattern seems to be better after all...
			if (inMode == BSDFContext::Mode::BSDF)
			{
				float3x3 tangent_space			= GenerateTangentSpace(inHitContext.NormalWS());
				float3 H						= Distribution::GGX::GenerateMicrofacetDirection(tangent_space, inHitContext, ioPathContext);
				float HdotV						= dot(H, inHitContext.ViewWS());

				float r_i, cos_theta_t, eta_it, eta_ti;
				F_Dielectric_Mitsuba(HdotV, inHitContext.Eta().x, r_i, cos_theta_t, eta_it, eta_ti);

				uint lobe_index					= RandomFloat01(ioPathContext.mRandomState) <= r_i ? sLobeIndexReflection : sLobeIndexRefraction;
				float3 L						= select(lobe_index == sLobeIndexReflection,
													reflect(-inHitContext.ViewWS(), inHitContext.NdotV() < 0 ? -H : H),
													refract(-inHitContext.ViewWS(), inHitContext.NdotV() < 0 ? -H : H, eta_ti));

				return BSDFContext::Generate(inMode, L, select(lobe_index == sLobeIndexReflection, 1.0, eta_it), lobe_index, inHitContext);
			}
			else
			{
				uint lobe_index					= inHitContext.NdotV() * dot(inHitContext.NormalWS(), inL) >= 0 ? sLobeIndexReflection : sLobeIndexRefraction;
				float3 L						= inL;
				float eta_it					= inHitContext.NdotV() >= 0.0f ? inHitContext.Eta().x : (1.0f / inHitContext.Eta().x);

				return BSDFContext::Generate(inMode, L, select(lobe_index == sLobeIndexReflection, 1.0, eta_it), lobe_index, inHitContext);
			}
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			BSDFResult result					= (BSDFResult)0;

			float r_i, cos_theta_t, eta_it, eta_ti;
			F_Dielectric_Mitsuba(inBSDFContext.mHdotV, inHitContext.Eta().x, r_i, cos_theta_t, eta_it, eta_ti);

			bool select_reflection				= inBSDFContext.mLobeIndex == sLobeIndexReflection;
			if (select_reflection)
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

				float microfacet_pdf			= D * inBSDFContext.mNdotH;
				float jacobian					= 1.0 / (4.0f * inBSDFContext.mHdotL);

				result.mBSDF					= D * G * F / (4.0f * inBSDFContext.mNdotV * inBSDFContext.mNdotL);
				result.mBSDFSamplePDF			= microfacet_pdf * jacobian;

				Inspect::DGF(ioPathContext, inBSDFContext, D, G, F);
			}
			else
			{
				// Based on RoughDieletric in Mitsuba, which is an implementation of [WMLT07] Microfacet Models for Refraction through Rough Surfaces
				// Omit roughness scale for now

				float D							= D_GGX(inBSDFContext.mNdotH, inHitContext.RoughnessAlpha());
				float G							= G_SmithGGX(inBSDFContext.mNdotL, inBSDFContext.mNdotV, inHitContext.RoughnessAlpha());
				float3 F						= (1.0 - r_i) * inHitContext.SpecularTransmittance();

				float microfacet_pdf			= D * inBSDFContext.mNdotH;
				float jacobian					= abs(Sqr(eta_it) * inBSDFContext.mHdotL / Sqr(inBSDFContext.mHdotV + eta_it * inBSDFContext.mHdotL));

				result.mBSDF					= abs(D * G * F * inBSDFContext.mHdotV * jacobian / (abs(inBSDFContext.mNdotV) * abs(inBSDFContext.mNdotL)));
				result.mBSDFSamplePDF			= microfacet_pdf * jacobian;

				Inspect::DGF(ioPathContext, inBSDFContext, D, G, F);
			}
			result.mBSDFSamplePDF				*= select(select_reflection, r_i, 1.0 - r_i);

			// See Dielectric::Evaluate
			result.mBSDF						*= select(select_reflection, 1.0, Sqr(eta_ti));
			result.mEta							= select(select_reflection, 1.0, eta_it);
			result.mMediumInstanceID			= select(inHitContext.HasMedium() && inBSDFContext.mNdotL < 0, inHitContext.mInstanceID, InvalidInstanceID);

			return result;
		}
	}

	namespace glTF
	{
		static uint sLobeIndexSpecular = 0;
		static uint sLobeIndexDiffuse = 1;

		BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inL, HitContext inHitContext, inout PathContext ioPathContext)
		{
			uint lobe_index						= ContextConstant::sLobeIndexAll;
			float3 L							= inL;
			if (inMode == BSDFContext::Mode::BSDF)
			{
				float3 specular_reflectance		= inHitContext.SpecularReflectance();
				float specular_probability		= MaxComponent(specular_reflectance); // [TODO] Better lobe selection probability? Fresnel based on N?
				lobe_index						= RandomFloat01(ioPathContext.mRandomState) <= specular_probability ? sLobeIndexSpecular : sLobeIndexDiffuse;
				if (lobe_index == sLobeIndexSpecular)
				{
					float3x3 tangent_space		= GenerateTangentSpace(inHitContext.NormalWS());
					float3 H					= Distribution::GGX::GenerateMicrofacetDirection(tangent_space, inHitContext, ioPathContext);
					float3 V					= inHitContext.ViewWS();
					float HdotV					= dot(H, V);
					L							= 2.0 * HdotV * H - V;
				}
				else
				{
					float3x3 tangent_space		= GenerateTangentSpace(inHitContext.NormalWS());
					float3 randome_direction	= RandomCosineDirection(ioPathContext.mRandomState);
					L							= normalize(randome_direction.x * tangent_space[0] + randome_direction.y * tangent_space[1] + randome_direction.z * tangent_space[2]);
				}
			}

			return BSDFContext::Generate(inMode, L, ContextConstant::sEtaITTrivial, lobe_index, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 specular_reflectance				= inHitContext.SpecularReflectance();
			float specular_probability				= MaxComponent(specular_reflectance);

			BSDFResult mixed_bsdf_result;
			mixed_bsdf_result.mBSDF					= 0;
			mixed_bsdf_result.mBSDFSamplePDF		= 0;
			mixed_bsdf_result.mEta					= 1.0;
			mixed_bsdf_result.mMediumInstanceID		= InvalidInstanceID;

			if (inBSDFContext.mLobeIndex == sLobeIndexDiffuse || inBSDFContext.mLobeIndex == ContextConstant::sLobeIndexAll)
			{
				BSDFResult diffuse_bsdf_result		= Diffuse::Evaluate(inBSDFContext, inHitContext, ioPathContext);
				mixed_bsdf_result.mBSDF				+= diffuse_bsdf_result.mBSDF;
				mixed_bsdf_result.mBSDFSamplePDF	+= diffuse_bsdf_result.mBSDFSamplePDF * (1.0 - specular_probability);
			}

			// Based on RoughConductor::Evaluate
			if (inBSDFContext.mLobeIndex == sLobeIndexSpecular || inBSDFContext.mLobeIndex == ContextConstant::sLobeIndexAll)
			{
				float D								= D_GGX(inBSDFContext.mNdotH, inHitContext.RoughnessAlpha());
				float G								= G_SmithGGX(inBSDFContext.mNdotL, inBSDFContext.mNdotV, inHitContext.RoughnessAlpha());
				float3 F							= F_Schlick(specular_reflectance, inBSDFContext.mHdotV);

				if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0 || inBSDFContext.mHdotL < 0 || inBSDFContext.mHdotV < 0)
					D								= 0;

				float microfacet_pdf				= D * inBSDFContext.mNdotH;
				float jacobian						= 1.0 / (4.0f * inBSDFContext.mHdotL);

				mixed_bsdf_result.mBSDF				+= D * G * F / (4.0f * inBSDFContext.mNdotV * inBSDFContext.mNdotL);
				mixed_bsdf_result.mBSDFSamplePDF	+= microfacet_pdf * jacobian * specular_probability;

				Inspect::DGF(ioPathContext, inBSDFContext, D, G, F);
			}

			return mixed_bsdf_result;
		}
	};

	namespace Explorer
	{
		BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inL, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 L = inL;
			if (inMode == BSDFContext::Mode::BSDF)
			{
				float3x3 tangent_space = GenerateTangentSpace(inHitContext.NormalWS());
				float3 randome_direction = RandomCosineDirection(ioPathContext.mRandomState);
				L = normalize(randome_direction.x * tangent_space[0] + randome_direction.y * tangent_space[1] + randome_direction.z * tangent_space[2]);
			}

			return BSDFContext::Generate(inMode, L, ContextConstant::sEtaITTrivial, ContextConstant::sLobeIndexTrivial, inHitContext);
		}

		BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			BSDFResult result;
			result.mBSDF = BRDFExplorer::BRDF(inBSDFContext.mL, inBSDFContext.mV, inBSDFContext.mN, inBSDFContext.mT, inBSDFContext.mB);
			result.mBSDFSamplePDF = max(0, inBSDFContext.mNdotL) / MATH_PI;
			result.mEta = 1.0;
			result.mMediumInstanceID = InvalidInstanceID;

			if (inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0)
				result.mBSDF = 0;

			return result;
		}
	};

	BSDFContext GenerateContext(BSDFContext::Mode inMode, float3 inL, HitContext inHitContext, inout PathContext ioPathContext)
	{
		switch (inHitContext.BSDF())
		{
#if USE_BSDF_Conductor
		case BSDF::Conductor:					return Conductor::GenerateContext(inMode, inL, inHitContext, ioPathContext); break;
#endif // USE_BSDF_Conductor
#if USE_BSDF_RoughConductor
		case BSDF::RoughConductor:				return RoughConductor::GenerateContext(inMode, inL, inHitContext, ioPathContext); break;
#endif // USE_BSDF_RoughConductor
#if USE_BSDF_Dielectric
		case BSDF::Dielectric:					return Dielectric::GenerateContext(inMode, inL, inHitContext, ioPathContext); break;
#endif // USE_BSDF_Dielectric
#if USE_BSDF_ThinDielectric
		case BSDF::ThinDielectric:				return Dielectric::GenerateContext(inMode, inL, inHitContext, ioPathContext); break;
#endif // USE_BSDF_ThinDielectric
#if USE_BSDF_RoughDielectric
		case BSDF::RoughDielectric:				return RoughDielectric::GenerateContext(inMode, inL, inHitContext, ioPathContext); break;
#endif // USE_BSDF_RoughDielectric
#if USE_BSDF_pbrMetallicRoughness
		case BSDF::pbrMetallicRoughness:		return glTF::GenerateContext(inMode, inL, inHitContext, ioPathContext); break;
#endif // USE_BSDF_pbrMetallicRoughness
		case BSDF::Explorer:					return Explorer::GenerateContext(inMode, inL, inHitContext, ioPathContext); break;
		case BSDF::Diffuse:						// [passthrough]
		default:								return Diffuse::GenerateContext(inMode, inL, inHitContext, ioPathContext); break;
		}
	}

	BSDFResult Evaluate(inout BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
	{
		BSDFResult result;
		switch (inHitContext.BSDF())
		{
#if USE_BSDF_Conductor
		case BSDF::Conductor:					result = Conductor::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
#endif // USE_BSDF_Conductor
#if USE_BSDF_RoughConductor
		case BSDF::RoughConductor:				result = RoughConductor::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
#endif // USE_BSDF_RoughConductor
#if USE_BSDF_Dielectric
		case BSDF::Dielectric:					result = Dielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
#endif // USE_BSDF_Dielectric
#if USE_BSDF_ThinDielectric
		case BSDF::ThinDielectric:				result = Dielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
#endif // USE_BSDF_ThinDielectric
#if USE_BSDF_RoughDielectric
		case BSDF::RoughDielectric:				result = RoughDielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
#endif // USE_BSDF_RoughDielectric
#if USE_BSDF_pbrMetallicRoughness
		case BSDF::pbrMetallicRoughness:		result = glTF::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
#endif // USE_BSDF_pbrMetallicRoughness
		case BSDF::Explorer:					result = Explorer::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::Diffuse:						// [passthrough]
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
