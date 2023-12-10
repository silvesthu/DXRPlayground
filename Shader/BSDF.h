#pragma once
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

// Context information about the hit point
struct HitContext
{
	BSDF			BSDF()
	{
		if (mConstants.mDebugInstanceIndex == mInstanceID)
		{
			switch (mConstants.mDebugInstanceMode)
			{
			case DebugInstanceMode::Barycentrics:				return BSDF::Diffuse;
			case DebugInstanceMode::Reflection:					return BSDF::Conductor;
			default: break;
			}
		}

		return InstanceDatas[mInstanceID].mBSDF;
	}
	uint			TwoSided()					{ return InstanceDatas[mInstanceID].mTwoSided; }
	float			Opacity()					{ return InstanceDatas[mInstanceID].mOpacity; }
	uint			LightIndex()				{ return InstanceDatas[mInstanceID].mLightIndex; }
	float			RoughnessAlpha()			{ return InstanceDatas[mInstanceID].mRoughnessAlpha; }
	float3			Albedo()						
	{
		uint albedo_texture_index = InstanceDatas[mInstanceID].mAlbedoTextureIndex;
		if (albedo_texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> albedo_texture	= ResourceDescriptorHeap[albedo_texture_index];
			return albedo_texture.SampleLevel(BilinearWrapSampler, mUV, 0).rgb;
		}
		return InstanceDatas[mInstanceID].mAlbedo; 
	}
	float3			SpecularReflectance()		{ return InstanceDatas[mInstanceID].mSpecularReflectance; }
	float3			SpecularTransmittance()		{ return InstanceDatas[mInstanceID].mSpecularTransmittance; }
	float3			Eta()						{ return InstanceDatas[mInstanceID].mEta; }
	float3			K()							{ return InstanceDatas[mInstanceID].mK; }
	float3			Emission()					{ return InstanceDatas[mInstanceID].mEmission; }

	bool			DiracDeltaDistribution()
	{
		switch (BSDF())
		{
		case BSDF::Dielectric:					return true;
		case BSDF::ThinDielectric:				return true;
		case BSDF::Conductor:					return true;
		default:								return false;
		}
	}

	float3			PositionWS()				{ return mRayOriginWS + mRayDirectionWS * mRayTCurrent; }
	float3			ViewWS()					{ return -mRayDirectionWS; }
	float3			NormalWS()
	{
		float3 view								= ViewWS();
		float3 normal							= mVertexNormalWS;

		// Handle TwoSided
		if (dot(normal, view) < 0 && TwoSided())
			normal								= -normal;

		return normal;
	}

	uint			mInstanceID;
	uint			mPrimitiveIndex;

	float3			mRayOriginWS;
	float3			mRayDirectionWS;
	float			mRayTCurrent;

	float3			mBarycentrics;
	float2			mUV;
	float3			mVertexPositionOS;
	float3			mVertexNormalOS;

	float3			mVertexNormalWS;
};

// Context information for BSDF evalution at the hit point
struct BSDFContext
{
	enum class Mode
	{
		BSDFSample,
		LightSample,
	};

	static BSDFContext Generate(BSDFContext::Mode inMode, float3 inLight, float3 inNormal, float3 inView, float inEta, HitContext inHitContext)
	{
		// [NOTE] All vectors should be in the same space
		//        [Mitsuba3] Does all calculation in tangent space where N = (0,0,1), then e.g NdotL = L.z

		BSDFContext bsdf_context;

		bsdf_context.mMode						= inMode;

		bsdf_context.mL							= inLight;
		bsdf_context.mN							= inNormal;
		bsdf_context.mV							= inView;
		bsdf_context.mH							= normalize(bsdf_context.mV + bsdf_context.mL * inEta);

		// [TODO] Need stricter handling of side of vectors

		bsdf_context.mNdotV						= dot(bsdf_context.mN, bsdf_context.mV);
		bsdf_context.mNdotL						= dot(bsdf_context.mN, bsdf_context.mL);
		bsdf_context.mNdotH						= dot(bsdf_context.mN, bsdf_context.mH);
		bsdf_context.mHdotV						= dot(bsdf_context.mH, bsdf_context.mV);
		bsdf_context.mHdotL						= dot(bsdf_context.mH, bsdf_context.mL);

		return bsdf_context;
	}

	Mode			mMode;

	float3			mL;
	float3			mN;
	float3			mV;
	float3			mH;

	float			mNdotH;
	float			mNdotV;
	float			mNdotL;
	float			mHdotV;
	float			mHdotL;
};

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
	//			sample:				Importance sample the BSDF model
	//			eval:				Evaluate the BSDF, multiply by the cosine foreshortening term
	//			pdf:				Compute the probability per unit solid angle of sampling a given direction
	//			eval_pdf:			{ eval, pdf }
	//			eval_pdf_sample:	{ eval_pdf, sample}
	//

	namespace Distribution
	{
		namespace GGX
		{
			float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
			{
				float a							= inHitContext.RoughnessAlpha();
				float a2						= a * a;

				float3 H; // Microfacet normal (Half-vector)
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

				// Assume N, H, V on the same side
				float3 V						= dot(inTangentSpace[2], inHitContext.mRayDirectionWS) < 0 ? -inHitContext.mRayDirectionWS : inHitContext.mRayDirectionWS;
				float HdotV						= dot(H, V);
				return 2.0 * HdotV * H - V;
			}
		}
	}

	namespace Diffuse
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 direction					= RandomCosineDirection(ioPathContext.mRandomState);
			return normalize(direction.x * inTangentSpace[0] + direction.y * inTangentSpace[1] + direction.z * inTangentSpace[2]);
		}

		BSDFResult Evaluate(BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			BSDFResult result;
			result.mBSDF						= inHitContext.Albedo() / MATH_PI;
			result.mBSDFSamplePDF				= max(0, inBSDFContext.mNdotL) / MATH_PI;
			result.mEta							= 1.0;

			if (inHitContext.BSDF() == BSDF::Unsupported)
			{
				result.mBSDF					= float3(1, 0, 1) / MATH_PI;
			}

			if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
			{
				result.mBSDF					= 0.0;
			}

			return result;
		}
	};

	namespace Conductor
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			return reflect(-inHitContext.ViewWS(), inHitContext.NormalWS());
		}

		BSDFResult Evaluate(BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 F							= F_Conductor_Mitsuba(inHitContext.Eta(), inHitContext.K(), inBSDFContext.mHdotV) * inHitContext.SpecularReflectance();

			DebugValue(PixelDebugMode::BSDF__D, ioPathContext.mRecursionCount, float3(nan(), 0, 0));
			DebugValue(PixelDebugMode::BSDF__G, ioPathContext.mRecursionCount, float3(nan(), 0, 0));
			DebugValue(PixelDebugMode::BSDF__F, ioPathContext.mRecursionCount, float3(F));

			BSDFResult result;
			result.mBSDF						= F;
			result.mBSDFSamplePDF				= 1.0;
			result.mEta							= 1.0;

			if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Reflection)
			{
				result.mBSDF					= 1.0;
			}

			return result;
		}
	}

	namespace RoughConductor
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			return Distribution::GGX::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		}

		BSDFResult Evaluate(BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float D								= D_GGX(inBSDFContext.mNdotH, inHitContext.RoughnessAlpha());
			float G								= G_SmithGGX(inBSDFContext.mNdotL, inBSDFContext.mNdotV, inHitContext.RoughnessAlpha());
			float3 F							= F_Conductor_Mitsuba(inHitContext.Eta(), inHitContext.K(), inBSDFContext.mHdotV) * inHitContext.SpecularReflectance();

			// [NOTE] Visible normal not supported yet
			//        [Mitsuba3] use visible normal sampling by default which affects both BSDF and BSDFPDF
			//        [TODO] Visible normal sampling not compatible with height-correlated visibility term?

			if (D < 0 || inBSDFContext.mNdotL < 0 || inBSDFContext.mNdotV < 0)
				D								= 0;

			DebugValue(PixelDebugMode::BSDF__D, ioPathContext.mRecursionCount, float3(D, 0, 0));
			DebugValue(PixelDebugMode::BSDF__G, ioPathContext.mRecursionCount, float3(G, 0, 0));
			DebugValue(PixelDebugMode::BSDF__F, ioPathContext.mRecursionCount, float3(F));

			// [NOTE] Sample may return BSDF * NdotL / PDF as a whole or in separated terms, which varies between implementations.
			//        Also, NdotL needs to be eliminated for Dirac delta distribution.
			//        [PBRT3] `Sample_f` return BSDF and PDF. Dirac delta distribution (e.g. BSDF_SPECULAR) divide an extra NdotL for BSDF. https://github.com/mmp/pbrt-v3/blob/aaa552a4b9cbf9dccb71450f47b268e0ed6370e2/src/core/reflection.cpp#L410
			//        [Mitsuba3] `sample` returns as whole. Dirac delta distribution (e.g. ) does not have the NdotL term. https://github.com/mitsuba-renderer/mitsuba3/blob/master/src/bsdfs/roughconductor.cpp#L226
			//		         [NOTE] In `sample`, assume m_sample_visible = false,
			//                      BSDF * NdotL / PDF = F * G * inBSDFContext.mHdotV / (inBSDFContext.mNdotV * inBSDFContext.mNdotH) * (D * inBSDFContext.mNdotH) / (4.0f * inBSDFContext.mHdotL)
			//			            HdotV and HdotL does not seems to cancel out at first glance.
			//                      The thing is, H is the half vector, HdotV == HdotL. (May differs due to floating point arithmetic though)
			//                      [TODO] Where does the HdotV come from in the first place?

			// [NOTE] Naming of wi/wo varies between implementations depending on point of view. The result should be same.
			//		  [PBRT3] has wo as V, wi as L
			// 		  [Mitsuba3] has wi as V, wo as L
			//        https://www.shadertoy.com/view/MsXfz4 has wi as V, wo as L

			BSDFResult result;
			result.mBSDF						= D * G * F / (4.0f * inBSDFContext.mNdotV * inBSDFContext.mNdotL);
			result.mBSDFSamplePDF				= (D * inBSDFContext.mNdotH) / (4.0f * inBSDFContext.mHdotL); // [NOTE] mHdotL == mHdotV
			result.mEta							= 1.0;
			return result;
		}
	}

	namespace Dielectric
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			// Dummy, will generate sampling direction in Evaluate
			return reflect(inHitContext.mRayDirectionWS, inTangentSpace[2]);
		}

		BSDFResult Evaluate(BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			// [NOTE] L and related information are not avaiable yet before selection between reflection and refraction

			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			F_Dielectric_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta.x, inBSDFContext.mNdotV, r_i, cos_theta_t, eta_it, eta_ti);

			if (inHitContext.BSDF() == BSDF::ThinDielectric)
			{
				// Account for internal reflections: r' = r + trt + tr^3t + ..
				// [NOTE] r' = r + trt + tr^3t + .. 
				//           = r + (1 - r) * r * (1-r) + (1-r) * r^3 * (1-r) + ...
				//			 = r + (1 - r) ^ 2 * r * \sum_0^\infty r^{2i}
				//			 = r * 2 / (r+1)
				r_i								*= 2.0f / (1.0f + r_i);

				// No change of direction for transmittion
				cos_theta_t						= inBSDFContext.mNdotV;
				eta_it							= 1.0f;
				eta_ti							= 1.0f;
			}

			bool selected_r						= RandomFloat01(ioPathContext.mRandomState) <= r_i;
			float3 L							= select(selected_r,
													reflect(-inBSDFContext.mV, inBSDFContext.mNdotV < 0 ? -inBSDFContext.mN : inBSDFContext.mN),
													refract(-inBSDFContext.mV, inBSDFContext.mNdotV < 0 ? -inBSDFContext.mN : inBSDFContext.mN, eta_ti));

			inBSDFContext						= BSDFContext::Generate(inBSDFContext.mMode, L, inBSDFContext.mN, inBSDFContext.mV, select(selected_r, 1.0, eta_it), inHitContext);

			BSDFResult result;
			result.mBSDF						= select(selected_r, r_i, 1.0 - r_i) * select(selected_r, InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance, InstanceDatas[inHitContext.mInstanceID].mSpecularTransmittance);
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
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			return Distribution::GGX::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		}

		BSDFResult Evaluate(BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
		{
			//float r_i;
			//float cos_theta_t;
			//float eta_it;
			//float eta_ti;
			//F_Dielectric_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta.x, inBSDFContext.mNdotV, r_i, cos_theta_t, eta_it, eta_ti);
	
			//bool selected_r = false;
			//if (inBSDFContext.mMode == BSDFContext::Mode::BSDFSample)
			//{
			//	selected_r = RandomFloat01(ioPathContext.mRandomState) <= r_i;

			//	float3 L = select(selected_r,
			//		reflect(-inBSDFContext.mV, inBSDFContext.mNdotV < 0 ? -inBSDFContext.mH : inBSDFContext.mH),
			//		refract(-inBSDFContext.mV, inBSDFContext.mNdotV < 0 ? -inBSDFContext.mH : inBSDFContext.mH, eta_ti));

			//	inBSDFContext = BSDFContext::Generate(inBSDFContext.mMode, L, inBSDFContext.mN, inBSDFContext.mV, select(selected_r, 1.0, eta_it), inHitContext);

			//	DebugValue(PixelDebugMode::L2, ioPathContext.mRecursionCount, float3(inBSDFContext.mL));
			//	DebugValue(PixelDebugMode::V2, ioPathContext.mRecursionCount, float3(inBSDFContext.mV));
			//	DebugValue(PixelDebugMode::N2, ioPathContext.mRecursionCount, float3(inBSDFContext.mN));
			//	DebugValue(PixelDebugMode::H2, ioPathContext.mRecursionCount, float3(inBSDFContext.mH));
			//}
			//else
			//{
			//	selected_r = inBSDFContext.mNdotV * inBSDFContext.mNdotL > 0;

			//	inBSDFContext = BSDFContext::Generate(inBSDFContext.mMode, inBSDFContext.mL, inBSDFContext.mN, inBSDFContext.mV, select(selected_r, 1.0, eta_it), inHitContext);

			//	DebugValue(PixelDebugMode::L3, ioPathContext.mRecursionCount, float3(inBSDFContext.mL));
			//	DebugValue(PixelDebugMode::V3, ioPathContext.mRecursionCount, float3(inBSDFContext.mV));
			//	DebugValue(PixelDebugMode::N3, ioPathContext.mRecursionCount, float3(inBSDFContext.mN));
			//	DebugValue(PixelDebugMode::H3, ioPathContext.mRecursionCount, float3(inBSDFContext.mH));
			//}

			//float D = D_GGX(inBSDFContext.mNdotH, inHitContext.RoughnessAlpha());
			//float G = G_SmithGGX(abs(inBSDFContext.mNdotL), abs(inBSDFContext.mNdotV), inHitContext.RoughnessAlpha());
			//float3 F = select(selected_r, InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance, InstanceDatas[inHitContext.mInstanceID].mSpecularTransmittance);

			//if (inBSDFContext.mMode == BSDFContext::Mode::BSDFSample)
			//{
			//	DebugValue(PixelDebugMode::BSDF__D, ioPathContext.mRecursionCount, float3(D, inBSDFContext.mNdotH, inHitContext.RoughnessAlpha()));
			//	DebugValue(PixelDebugMode::BSDF__G, ioPathContext.mRecursionCount, float3(G, 0, 0));
			//	DebugValue(PixelDebugMode::BSDF__F, ioPathContext.mRecursionCount, float3(F));
			//}
			//else
			//{
			//	DebugValue(PixelDebugMode::Light_D, ioPathContext.mRecursionCount, float3(D, inBSDFContext.mNdotH, inHitContext.RoughnessAlpha()));
			//	DebugValue(PixelDebugMode::Light_G, ioPathContext.mRecursionCount, float3(G, 0, 0));
			//	DebugValue(PixelDebugMode::Light_F, ioPathContext.mRecursionCount, float3(F));
			//}

			//inBSDFContext.mBSDF = abs(select(selected_r, 
			//	D * G * F / (4.0f * inBSDFContext.mNdotV * inBSDFContext.mNdotL),
			//	D * G * F * inBSDFContext.mHdotV * (sqr(eta_it) * inBSDFContext.mHdotL) / (inBSDFContext.mNdotV * inBSDFContext.mNdotL * sqr(inBSDFContext.mHdotV + eta_it * inBSDFContext.mHdotL))));
			//inBSDFContext.mBSDF *= select(selected_r, r_i, 1.0 - r_i);

			//inBSDFContext.mBSDFSamplePDF = abs(select(selected_r, 
			//	(D * inBSDFContext.mNdotH) / (4.0f * inBSDFContext.mHdotL),
			//	(D * inBSDFContext.mNdotH) * (sqr(eta_it) * inBSDFContext.mHdotL) / sqr(inBSDFContext.mHdotV + eta_it * inBSDFContext.mHdotL)));
			//inBSDFContext.mBSDFSamplePDF *= select(selected_r, r_i, 1.0 - r_i);

			//// See Dielectric::Evaluate
			//inBSDFContext.mBSDF *= select(selected_r, 1.0, sqr(eta_ti));
			//inBSDFContext.mEta = select(selected_r, 1.0, eta_it);

			BSDFResult result;
			result.mBSDF						= 0;
			result.mBSDFSamplePDF				= 0;
			result.mEta							= 0;
			return result;
		}
	}

	BSDFContext GenerateImportanceSamplingContext(HitContext inHitContext, inout PathContext ioPathContext)
	{
		float3x3 tangent_space					= GenerateTangentSpace(inHitContext.NormalWS());
		float3 importance_sampling_direction	= 0;
		switch (inHitContext.BSDF())
		{
		case BSDF::Unsupported:					// [[fallthrough]];
		case BSDF::Diffuse:						importance_sampling_direction = Diffuse::GenerateImportanceSamplingDirection(tangent_space, inHitContext, ioPathContext); break;
		case BSDF::Conductor:					importance_sampling_direction = Conductor::GenerateImportanceSamplingDirection(tangent_space, inHitContext, ioPathContext); break;
		case BSDF::RoughConductor:				importance_sampling_direction = RoughConductor::GenerateImportanceSamplingDirection(tangent_space, inHitContext, ioPathContext); break;
		case BSDF::Dielectric:					importance_sampling_direction = Dielectric::GenerateImportanceSamplingDirection(tangent_space, inHitContext, ioPathContext); break;
		case BSDF::ThinDielectric:				importance_sampling_direction = Dielectric::GenerateImportanceSamplingDirection(tangent_space, inHitContext, ioPathContext); break;
		case BSDF::RoughDielectric:				importance_sampling_direction = RoughDielectric::GenerateImportanceSamplingDirection(tangent_space, inHitContext, ioPathContext); break;
		default:								break;
		}

 		BSDFContext bsdf_context				= BSDFContext::Generate(BSDFContext::Mode::BSDFSample, importance_sampling_direction, inHitContext.NormalWS(), inHitContext.ViewWS(), 1.0, inHitContext);
		return bsdf_context;
	}

	BSDFResult Evaluate(BSDFContext inBSDFContext, HitContext inHitContext, inout PathContext ioPathContext)
	{
		if (inBSDFContext.mMode == BSDFContext::Mode::BSDFSample)
		{
			DebugValue(PixelDebugMode::BRDF__L, ioPathContext.mRecursionCount, float3(inBSDFContext.mL));
			DebugValue(PixelDebugMode::BRDF__V, ioPathContext.mRecursionCount, float3(inBSDFContext.mV));
			DebugValue(PixelDebugMode::BRDF__N, ioPathContext.mRecursionCount, float3(inBSDFContext.mN));
			DebugValue(PixelDebugMode::BRDF__H, ioPathContext.mRecursionCount, float3(inBSDFContext.mH));
		}
		else
		{
			DebugValue(PixelDebugMode::Light_L, ioPathContext.mRecursionCount, float3(inBSDFContext.mL));
			DebugValue(PixelDebugMode::Light_V, ioPathContext.mRecursionCount, float3(inBSDFContext.mV));
			DebugValue(PixelDebugMode::Light_N, ioPathContext.mRecursionCount, float3(inBSDFContext.mN));
			DebugValue(PixelDebugMode::Light_H, ioPathContext.mRecursionCount, float3(inBSDFContext.mH));
		}

		BSDFResult result;
		switch (inHitContext.BSDF())
		{
		case BSDF::Diffuse:						result = Diffuse::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::Conductor:					result = Conductor::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::RoughConductor:				result = RoughConductor::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::Dielectric:					result = Dielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::ThinDielectric:				result = Dielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
		case BSDF::RoughDielectric:				result = RoughDielectric::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
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