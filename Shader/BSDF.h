#pragma once
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

namespace BSDFEvaluation
{
	BSDFContext GenerateContext(float3 inLight, float3 inNormal, float3 inView, HitContext inHitContext)
	{
		// [NOTE] All vectors should be in the same space
		//        [Mitsuba3] Does all calculation in tangent space where N = (0,0,1), then e.g NdotL = L.z

		BSDFContext bsdf_context;

		bsdf_context.mImportanceSamplingDirection = false;

		bsdf_context.mL = inLight;
		bsdf_context.mN = inNormal;
		bsdf_context.mV = inView;
		bsdf_context.mH = normalize((inLight + inView) / 2.0);

		bsdf_context.mNdotH = dot(bsdf_context.mN, bsdf_context.mH);
		bsdf_context.mNdotV = dot(bsdf_context.mN, bsdf_context.mV);
		bsdf_context.mNdotL = dot(bsdf_context.mN, bsdf_context.mL);
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

				// Microfacet
				float3 H; // Microfacet normal (Half-vector)
				{
					float e0 = RandomFloat01(ioPathContext.mRandomState);
					float e1 = RandomFloat01(ioPathContext.mRandomState);

					// 2D Distribution -> GGX Distribution (Polar)
					float cos_theta = sqrt((1.0 - e0) / ((a2 - 1) * e0 + 1.0));
					float sin_theta = sqrt(1 - cos_theta * cos_theta);
					float phi = 2 * MATH_PI * e1;

					// Polar -> Cartesian
					H.x = sin_theta * cos(phi);
					H.y = sin_theta * sin(phi);
					H.z = cos_theta;

					// Tangent -> World
					H = normalize(H.x * inTangentSpace[0] + H.y * inTangentSpace[1] + H.z * inTangentSpace[2]);
				}

				// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(H == inTangentSpace[2], 0));

				float3 V = -sGetWorldRayDirection();
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

			// [NOTE] Sample may return BSDF * NdotL / PDF as a whole or in separated terms vary between implementations.
			//        Also, NdotL needs to be eliminated for Dirac delta distribution.
			//        [PBRT3] `Sample_f` return BSDF and PDF. Dirac delta distribution (e.g. BSDF_SPECULAR) divide an extra NdotL for BSDF. https://github.com/mmp/pbrt-v3/blob/aaa552a4b9cbf9dccb71450f47b268e0ed6370e2/src/core/reflection.cpp#L410
			//        [Mitsuba3] `sample` returns as whole. Dirac delta distribution (e.g. ) does not have the NdotL term. https://github.com/mitsuba-renderer/mitsuba3/blob/master/src/bsdfs/roughconductor.cpp#L226
			//		         [NOTE] In `sample`, assume m_sample_visible = false,
			//                      BSDF * NdotL / PDF = F * G * ioBSDFContext.mHdotV / (ioBSDFContext.mNdotV * ioBSDFContext.mNdotH) * (D * ioBSDFContext.mNdotH) / (4.0f * ioBSDFContext.mHdotL)
			//			            HdotV and HdotL does not seems to cancel out at first glance.
			//                      The thing is, H is the half vector, HdotV == HdotL. (May differs due to floating point arithmetic though)
			//                      [TODO] Where does the HdotV come from in the first place?

			// [NOTE] Naming of wi/wo vary between implementations depending on point of view. The result should be same.
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

			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mN, 0));
			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(r_i, cos_theta_t, eta_it, eta_ti));

			bool selected_r = RandomFloat01(ioPathContext.mRandomState) <= r_i;
			ioBSDFContext.mN = ioBSDFContext.mNdotV < 0 ? -ioBSDFContext.mN : ioBSDFContext.mN;
			float3 L = select(selected_r,
				reflect(-ioBSDFContext.mV, ioBSDFContext.mN),
				refract(-ioBSDFContext.mV, ioBSDFContext.mN, eta_ti));

			ioBSDFContext = GenerateContext(L, ioBSDFContext.mN, ioBSDFContext.mV, inHitContext);
			ioBSDFContext.mBSDF = select(selected_r, InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance, InstanceDatas[inHitContext.mInstanceID].mSpecularTransmittance) * select(selected_r, r_i, 1.0 - r_i);
			ioBSDFContext.mBSDFPDF = select(selected_r, r_i, 1.0 - r_i);

			// [NOTE] Account for solid angle compression
			//        [Mitsuba3] > For transmission, radiance must be scaled to account for the solid angle compression that occurs when crossing the interface. 
			//                   https://github.com/mitsuba-renderer/mitsuba3/blob/master/src/bsdfs/dielectric.cpp#L359
			//        [PBRT3] > Account for non-symmetry with transmission to different medium 
			//	              https://github.com/mmp/pbrt-v3/blob/aaa552a4b9cbf9dccb71450f47b268e0ed6370e2/src/core/reflection.cpp#L163
			//		          https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/The_Path-Space_Measurement_Equation#x3-Non-symmetryDuetoRefraction
			// [NOTE] Output eta (inverse) to remove its effect on Russian Roulette. As Russian Roulette depends on throughput, which in turn depends on BSDF.
			//        The difference is easily observable when Russian Roulette is enabled even for the first iterations.
			//		  [PBRT3] > It lets us sometimes avoid terminating refracted rays that are about to be refracted back out of a medium and thus have their beta value increased.
			//                https://github.com/mmp/pbrt-v3/blob/master/src/integrators/path.cpp#L72
			ioBSDFContext.mBSDF *= select(selected_r, 1.0, sqr(eta_ti));
			ioBSDFContext.mEta = select(selected_r, 1.0, eta_it);

			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(-ioBSDFContext.mV, selected_r));
			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(L, 0));
		}
	}

	namespace ThinDielectric
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			// Dummy, will generate sampling direction in Evaluate
			return reflect(inHitContext.mRayDirectionWS, inTangentSpace[2]);
		}

		void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			F_Dielectric_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta.x, ioBSDFContext.mNdotV, r_i, cos_theta_t, eta_it, eta_ti);

			// [NOTE] Thin Dielectric (other than these, calculation is identical to Dielectric)
			//		  [Mitsuba3] 
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
			ioBSDFContext.mN = ioBSDFContext.mNdotV < 0 ? -ioBSDFContext.mN : ioBSDFContext.mN;
			float3 L = select(selected_r,
				reflect(-ioBSDFContext.mV, ioBSDFContext.mN),
				refract(-ioBSDFContext.mV, ioBSDFContext.mN, eta_ti));

			ioBSDFContext = GenerateContext(L, ioBSDFContext.mN, ioBSDFContext.mV, inHitContext);
			ioBSDFContext.mBSDF = select(selected_r, InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance, InstanceDatas[inHitContext.mInstanceID].mSpecularTransmittance) * select(selected_r, r_i, 1.0 - r_i);
			ioBSDFContext.mBSDFPDF = select(selected_r, r_i, 1.0 - r_i);
			
			ioBSDFContext.mBSDF *= select(selected_r, 1.0, sqr(eta_ti));
			ioBSDFContext.mEta = select(selected_r, 1.0, eta_it);
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
			bool importance_sampling = ioBSDFContext.mImportanceSamplingDirection;

			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			F_Dielectric_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta.x, ioBSDFContext.mHdotV, r_i, cos_theta_t, eta_it, eta_ti);

			// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mHdotV, 0, 0, r_i));

			// [TODO] Fix sample light...

			bool selected_r = select(ioBSDFContext.mImportanceSamplingDirection, 
				RandomFloat01(ioPathContext.mRandomState) <= r_i,
				(ioBSDFContext.mNdotV * ioBSDFContext.mNdotL) > 0);
			ioBSDFContext.mN = select(selected_r, ioBSDFContext.mN, -ioBSDFContext.mN);
			ioBSDFContext.mH = select(selected_r, ioBSDFContext.mH, -ioBSDFContext.mH);
			float3 L = select(selected_r,
				reflect(-ioBSDFContext.mV, ioBSDFContext.mH),
				refract(-ioBSDFContext.mV, ioBSDFContext.mH, eta_ti));

			if (importance_sampling)
			{
				// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(selected_r, 0, 0, 1));
			}
			else
			{
				// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(selected_r, 0, 0, -1));
				// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(L, -1));
				// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mH, -1));
			}

			ioBSDFContext = GenerateContext(L, ioBSDFContext.mN, ioBSDFContext.mV, inHitContext);

			float D = D_GGX(ioBSDFContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float G = G_SmithGGX(ioBSDFContext.mNdotL, ioBSDFContext.mNdotV, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);

			if (D < 0 || ioBSDFContext.mNdotL < 0 || ioBSDFContext.mNdotV < 0)
				D = 0;

			float3 F = select(selected_r, InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance, InstanceDatas[inHitContext.mInstanceID].mSpecularTransmittance);

			ioBSDFContext.mBSDF = select(selected_r, 
				D * G * F / (4.0f * ioBSDFContext.mNdotV * ioBSDFContext.mNdotL),
				D * G * F * (sqr(eta_it) * ioBSDFContext.mHdotV * ioBSDFContext.mHdotL) / (ioBSDFContext.mNdotV * ioBSDFContext.mNdotL * sqr(ioBSDFContext.mHdotV + eta_it * ioBSDFContext.mHdotL)));
			ioBSDFContext.mBSDF *= select(selected_r, r_i, 1.0 - r_i);

			ioBSDFContext.mBSDFPDF = select(selected_r, 
				(D * ioBSDFContext.mNdotH) / (4.0f * ioBSDFContext.mHdotL),
				(D * ioBSDFContext.mNdotH) * (sqr(eta_it) * ioBSDFContext.mHdotL) / sqr(ioBSDFContext.mHdotV + eta_it * ioBSDFContext.mHdotL));
			ioBSDFContext.mBSDFPDF *= select(selected_r, r_i, 1.0 - r_i);

			if (importance_sampling)
			{
				// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mBSDF, ioBSDFContext.mBSDFPDF));
			}
			else
			{
				// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mBSDF, ioBSDFContext.mBSDFPDF));
			}

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
			ioBSDFContext = GenerateContext(reflect(-ioBSDFContext.mV, ioBSDFContext.mN), ioBSDFContext.mN, ioBSDFContext.mV, inHitContext);
			ioBSDFContext.mBSDF = 1.0f;
			ioBSDFContext.mBSDFPDF = 1.0f;
		}
	}

	BSDFType GetBSDFType(HitContext inHitContext)
	{
		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID)
		{
			switch (mConstants.mDebugInstanceMode)
			{
			case DebugInstanceMode::Barycentrics: return BSDFType::DebugEmissive;
			case DebugInstanceMode::Mirror: return BSDFType::DebugMirror;
			default: break;
			}
		}

		return InstanceDatas[inHitContext.mInstanceID].mBSDFType;
	}

	bool DiracDeltaDistribution(HitContext inHitContext)
	{
		switch (GetBSDFType(inHitContext))
		{
		case BSDFType::Dielectric:		return true;
		case BSDFType::ThinDielectric:	return true;
		case BSDFType::DebugMirror:		return true;
		default:						return false;
		}
	}

	float3 GenerateImportanceSamplingDirection(float3 inNormal, HitContext inHitContext, inout PathContext ioPathContext)
	{
		// [TODO] Refactor this as sample micro distribution and return H

		float3x3 inTangentSpace = GenerateTangentSpace(inNormal);
		[branch]
		switch (GetBSDFType(inHitContext))
		{
		case BSDFType::Unsupported:		// [[fallthrough]];
		case BSDFType::Diffuse:			return Lambert::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDFType::RoughConductor:	return RoughConductor::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDFType::Dielectric:		return Dielectric::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDFType::ThinDielectric:	return ThinDielectric::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDFType::RoughDielectric:	return RoughDielectric::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		default:						return 0;
		}
	}

	BSDFContext GenerateImportanceSamplingContext(float3 inNormal, float3 inView, HitContext inHitContext, inout PathContext ioPathContext)
	{
		float3 importance_sampling_direction = GenerateImportanceSamplingDirection(inNormal, inHitContext, ioPathContext);
		BSDFContext bsdf_context = BSDFEvaluation::GenerateContext(importance_sampling_direction, inHitContext.mVertexNormalWS, -inHitContext.mRayDirectionWS, inHitContext);
		bsdf_context.mImportanceSamplingDirection = true;
		return bsdf_context;
	}

	void Evaluate(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
	{
		[branch]
		switch (GetBSDFType(inHitContext))
		{
		case BSDFType::Diffuse:			Lambert::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::RoughConductor:	RoughConductor::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::Dielectric:		Dielectric::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::ThinDielectric:	ThinDielectric::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::RoughDielectric:	RoughDielectric::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::DebugEmissive:	DebugEmissive::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::DebugMirror:		DebugMirror::Evaluate(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::Unsupported:		// [[fallthrough]];
		default:						break;
		}

		if (DiracDeltaDistribution(inHitContext))
		{
			// [NOTE] For Dirac delta distribution, the cosine term is defined to be canceled out with the one in integration
			// Considering it as part of integral to describe illuminance sounds okay, but still not very thorough... 
			// https://www.pbr-book.org/3ed-2018/Reflection_Models/Specular_Reflection_and_Transmission
			// https://stackoverflow.com/questions/22431912/path-tracing-why-is-there-no-cosine-term-when-calculating-perfect-mirror-reflec
			// https://gamedev.net/forums/topic/657520-cosine-term-in-rendering-equation/5159311/?page=2
			ioBSDFContext.mBSDF /= ioBSDFContext.mNdotL;
		}

		// DebugValue(PixelDebugMode::Manual, ioPathContext.mRecursionCount, float4(ioBSDFContext.mBSDF, ioBSDFContext.mBSDFPDF));
	}
}