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
		uint texture_index = InstanceDatas[mInstanceID].mAlbedoTexture.mTextureIndex;
		uint sampler_index = InstanceDatas[mInstanceID].mAlbedoTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture	= ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * InstanceDatas[mInstanceID].mAlbedo;
		}
		return InstanceDatas[mInstanceID].mAlbedo; 
	}
	float3			SpecularReflectance()
	{
		uint texture_index = InstanceDatas[mInstanceID].mReflectanceTexture.mTextureIndex;
		uint sampler_index = InstanceDatas[mInstanceID].mReflectanceTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * InstanceDatas[mInstanceID].mReflectance;
		}
		return InstanceDatas[mInstanceID].mReflectance;
	}
	float3			SpecularTransmittance()		{ return InstanceDatas[mInstanceID].mSpecularTransmittance; }
	float3			Eta()						{ return InstanceDatas[mInstanceID].mEta; }
	float3			K()							{ return InstanceDatas[mInstanceID].mK; }
	float3			Emission()					
	{
		uint texture_index = InstanceDatas[mInstanceID].mEmissionTexture.mTextureIndex;
		uint sampler_index = InstanceDatas[mInstanceID].mEmissionTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * InstanceDatas[mInstanceID].mEmission;
		}
		return InstanceDatas[mInstanceID].mEmission; 
	}

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
	float3			DirectionWS()				{ return mRayDirectionWS; }
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
	float3			Barycentrics()				{ return mBarycentrics; }
	float2			UV()						{ return mUV; }
	float			NdotV()						{ return dot(NormalWS(), ViewWS()); }

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
	// [NOTE] All vectors should be in the same space. Tangent space seems to be the best fit, where N = (0,0,1), then e.g NdotL = L.z
	//     [Mitsuba3] Tangent space

	enum class Mode
	{
		BSDF,
		Light,
	};
	Mode			mMode;

	static BSDFContext Generate(Mode inMode, float3 inLight, float inEtaIT, bool inLobe0Selected, HitContext inHitContext)
	{
		BSDFContext bsdf_context;

		bsdf_context.mMode						= inMode;

		bsdf_context.mL							= inLight;
		bsdf_context.mN							= inHitContext.NormalWS();
		bsdf_context.mV							= inHitContext.ViewWS();
		bsdf_context.mH							= normalize(bsdf_context.mV + bsdf_context.mL * inEtaIT);	// See roughdielectric::eval

		if (dot(bsdf_context.mN, bsdf_context.mH) < 0)
			bsdf_context.mH						= -bsdf_context.mH; // Put H on the same side as N
	
		bsdf_context.mNdotV						= dot(bsdf_context.mN, bsdf_context.mV);
		bsdf_context.mNdotL						= dot(bsdf_context.mN, bsdf_context.mL);
		bsdf_context.mNdotH						= dot(bsdf_context.mN, bsdf_context.mH);
		bsdf_context.mHdotV						= dot(bsdf_context.mH, bsdf_context.mV);
		bsdf_context.mHdotL						= dot(bsdf_context.mH, bsdf_context.mL);

		bsdf_context.mLobe0Selected				= inLobe0Selected;

		return bsdf_context;
	}

	static BSDFContext Generate(Mode inMode, float3 inLight, HitContext inHitContext)
	{
		float dummy_eta_it						= 1.0;
		bool dummy_lobe0_selected				= false;

		BSDFContext bsdf_context				= Generate(inMode, inLight, dummy_eta_it, dummy_lobe0_selected, inHitContext);

		// Patch
		bsdf_context.mLobe0Selected				= bsdf_context.mNdotV * bsdf_context.mNdotL > 0;
		// [NOTE] eta_it can not be determined until BSDF is evaluated. SetEta is used then.

		return bsdf_context;
	}

	void			SetEta(float inEtaIT)
	{
		mH										= normalize(mV + mL * inEtaIT);	// See roughdielectric::eval
		if (dot(mN, mH) < 0)
			mH									= -mH; // Put H on the same side as N

		mNdotH									= dot(mN, mH);
		mHdotV									= dot(mH, mV);
		mHdotL									= dot(mH, mL);
	}

	void			FlipNormal()
	{
		mN										= -mN;
		mH										= -mH;

		mNdotV									= -mNdotV;
		mNdotL									= -mNdotL;

		mHdotV									= -mHdotV;
		mHdotL									= -mHdotL;
	}

	float3			mL;
	float3			mN;
	float3			mV;
	float3			mH;

	float			mNdotH;
	float			mNdotV;
	float			mNdotL;
	float			mHdotV;
	float			mHdotL;

	bool			mLobe0Selected;				// [TODO] More than 2 lobes? Use lobe index? Still coupled with implementaion detail

	float			mLPDF;						// For light sample
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
				DebugValue(PixelDebugMode::BSDF__D, ioPathContext.mRecursionDepth, float3(nan(), 0, 0));
				DebugValue(PixelDebugMode::BSDF__G, ioPathContext.mRecursionDepth, float3(nan(), 0, 0));
				DebugValue(PixelDebugMode::BSDF__F, ioPathContext.mRecursionDepth, float3(F));
			}
			else
			{
				DebugValue(PixelDebugMode::Light_D, ioPathContext.mRecursionDepth, float3(nan(), 0, 0));
				DebugValue(PixelDebugMode::Light_G, ioPathContext.mRecursionDepth, float3(nan(), 0, 0));
				DebugValue(PixelDebugMode::Light_F, ioPathContext.mRecursionDepth, float3(F));
			}

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
				DebugValue(PixelDebugMode::BSDF__D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
				DebugValue(PixelDebugMode::BSDF__G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
				DebugValue(PixelDebugMode::BSDF__F, ioPathContext.mRecursionDepth, float3(F));
			}
			else
			{
				DebugValue(PixelDebugMode::Light_D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
				DebugValue(PixelDebugMode::Light_G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
				DebugValue(PixelDebugMode::Light_F, ioPathContext.mRecursionDepth, float3(F));
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
			{
				DebugValue(PixelDebugMode::BSDF__I, ioPathContext.mRecursionDepth, float3(selected_r ? 0 : 1, 0, 0));
			}
			else
			{
				DebugValue(PixelDebugMode::Light_I, ioPathContext.mRecursionDepth, float3(selected_r ? 0 : 1, 0, 0));
			}

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
					DebugValue(PixelDebugMode::BSDF__D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
					DebugValue(PixelDebugMode::BSDF__G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
					DebugValue(PixelDebugMode::BSDF__F, ioPathContext.mRecursionDepth, float3(F));
				}
				else
				{
					DebugValue(PixelDebugMode::Light_D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
					DebugValue(PixelDebugMode::Light_G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
					DebugValue(PixelDebugMode::Light_F, ioPathContext.mRecursionDepth, float3(F));
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
					DebugValue(PixelDebugMode::BSDF__D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
					DebugValue(PixelDebugMode::BSDF__G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
					DebugValue(PixelDebugMode::BSDF__F, ioPathContext.mRecursionDepth, float3(F));
				}
				else
				{
					DebugValue(PixelDebugMode::Light_D, ioPathContext.mRecursionDepth, float3(D, 0, 0));
					DebugValue(PixelDebugMode::Light_G, ioPathContext.mRecursionDepth, float3(G, 0, 0));
					DebugValue(PixelDebugMode::Light_F, ioPathContext.mRecursionDepth, float3(F));
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
		case BSDF::glTF:						bsdf_context = Diffuse::GenerateContext(inHitContext, ioPathContext); break;
		default:								bsdf_context = Diffuse::GenerateContext(inHitContext, ioPathContext); break;
		}

		DebugValue(PixelDebugMode::BSDF__L,		ioPathContext.mRecursionDepth, float3(bsdf_context.mL));
		DebugValue(PixelDebugMode::BSDF__V,		ioPathContext.mRecursionDepth, float3(bsdf_context.mV));
		DebugValue(PixelDebugMode::BSDF__N,		ioPathContext.mRecursionDepth, float3(bsdf_context.mN));
		DebugValue(PixelDebugMode::BSDF__H,		ioPathContext.mRecursionDepth, float3(bsdf_context.mH));

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
		case BSDF::glTF:						result = Diffuse::Evaluate(inBSDFContext, inHitContext, ioPathContext); break;
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