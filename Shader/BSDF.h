#pragma once
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

#include "RayQuery.h"

namespace BSDFEvaluation
{
	BSDFContext GenerateContext(float3 inLight, float3 inNormal, float3 inView, HitContext inHitContext)
	{
		BSDFContext bsdf_context;

		bsdf_context.mL = inLight;
		bsdf_context.mN = inNormal;
		bsdf_context.mV = inView;
		bsdf_context.mH = normalize((inLight + inView) / 2.0);

		bsdf_context.mNdotH = dot(bsdf_context.mN, bsdf_context.mH);
		bsdf_context.mNdotV = dot(bsdf_context.mN, bsdf_context.mV);
		bsdf_context.mNdotL = dot(bsdf_context.mN, bsdf_context.mL);
		bsdf_context.mHdotV = dot(bsdf_context.mH, bsdf_context.mV);
		bsdf_context.mHdotL = dot(bsdf_context.mH, bsdf_context.mL);

		bsdf_context.mDeltaDistribution = false;

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

	namespace Lambert
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			float3 direction = RandomCosineDirection(ioPathContext.mRandomState);
			return normalize(direction.x * inTangentSpace[0] + direction.y * inTangentSpace[1] + direction.z * inTangentSpace[2]);
		}

		void SampleDirection(HitContext inHitContext, inout BSDFContext ioBSDFContext)
		{
			ioBSDFContext.mNdotL = max(0, ioBSDFContext.mNdotL);

			ioBSDFContext.mBSDF = BSDFEvaluation::Source::Albedo(inHitContext) * ioBSDFContext.mNdotL / MATH_PI;
			ioBSDFContext.mBSDFPDF = ioBSDFContext.mNdotL / MATH_PI;
		}

		void SampleBSDF(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			ioBSDFContext.mNdotL = max(0, ioBSDFContext.mNdotL);

			ioBSDFContext.mBSDF = BSDFEvaluation::Source::Albedo(inHitContext) * ioBSDFContext.mNdotL / MATH_PI;
			ioBSDFContext.mBSDFPDF = ioBSDFContext.mNdotL / MATH_PI;
		}
	};

	namespace RoughConductor
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

			float3 V = -sGetWorldRayDirection();
			float HdotV = dot(H, V);
			return 2.0 * HdotV * H - V;
		}

		void SampleDirection(HitContext inHitContext, inout BSDFContext ioBSDFContext)
		{
			float D = D_GGX(ioBSDFContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float G = G_SmithGGX(ioBSDFContext.mNdotL, ioBSDFContext.mNdotV, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float3 F = F_Conductor_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta, InstanceDatas[inHitContext.mInstanceID].mK, ioBSDFContext.mHdotV) * InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance;

			if (D < 0 || ioBSDFContext.mNdotL < 0 || ioBSDFContext.mNdotV < 0)
				D = 0;

			ioBSDFContext.mBSDF = D * G * F / (4.0f * ioBSDFContext.mNdotV);
			ioBSDFContext.mBSDFPDF = (D * ioBSDFContext.mNdotH) / (4.0f * ioBSDFContext.mHdotV);
		}

		void SampleBSDF(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			float D = D_GGX(ioBSDFContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float G = G_SmithGGX(ioBSDFContext.mNdotL, ioBSDFContext.mNdotV, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float3 F = F_Conductor_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta, InstanceDatas[inHitContext.mInstanceID].mK, ioBSDFContext.mHdotV) * InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance;

			// [NOTE] Mitsuba3 use visible normal sampling by default which affects both BSDF and BSDFPDF
			// [TODO] Visible normal sampling not compatible with height-correlated visibility term?

			if (D < 0 || ioBSDFContext.mNdotL < 0 || ioBSDFContext.mNdotV < 0)
				D = 0;

			ioBSDFContext.mBSDF = D * G * F / (4.0f * ioBSDFContext.mNdotV);
			ioBSDFContext.mBSDFPDF = (D * ioBSDFContext.mNdotH) / (4.0f * ioBSDFContext.mHdotV);

			// [NOTE] PBRT3 has wo as V, wi as L
			// [NOTE] Mitsuba3 has wo as V, wi as L
			// [NOTE] https://www.shadertoy.com/view/MsXfz4 has wi as V, wo as L
		}
	}

	namespace Dielectric
	{
		float3 GenerateImportanceSamplingDirection(float3x3 inTangentSpace, HitContext inHitContext, inout PathContext ioPathContext)
		{
			// Dummy, will generate sampling direction in SampleBSDF

			return reflect(inHitContext.mRayDirectionWS, inTangentSpace[2]);
		}

		void SampleDirection(HitContext inHitContext, inout BSDFContext ioBSDFContext)
		{
			// No light sample supported

			ioBSDFContext.mBSDF = 0.0;
			ioBSDFContext.mBSDFPDF = 0.0;
		}

		float3 refract1(float3 wi, float cos_theta_t, float eta_ti) 
		{
			return float3(-eta_ti * wi.x, -eta_ti * wi.y, cos_theta_t);
		}

		void SampleBSDF(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
		{
			// [NOTE] L and related information are not avaiable yet before selection between reflection and refraction

			float r_i;
			float cos_theta_t;
			float eta_it;
			float eta_ti;
			F_Dielectric_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta.x, ioBSDFContext.mNdotV, r_i, cos_theta_t, eta_it, eta_ti);

			float n2 = InstanceDatas[inHitContext.mInstanceID].mEta.x;
			float n1 = 1.0 / n2;

			float cos_i = abs(ioBSDFContext.mNdotV);
			float cos_t = safe_sqrt(1 - ((n1 * n1) * (1 - cos_i * cos_i) / (n2 * n2)));

			float R_s = (n1 * cos_i - n2 * cos_t) / (n1 * cos_i + n2 * cos_t);
			R_s *= R_s;
			float R_p = (n1 * cos_t - n2 * cos_i) / (n1 * cos_t + n2 * cos_i);
			R_p *= R_p;

			float R_eff = (R_s + R_p) / 2.0;

			r_i = R_eff;
			eta_ti = ioBSDFContext.mNdotV < 0 ? n2 : n1;

			bool selected_r = RandomFloat01(ioPathContext.mRandomState) <= r_i;
			ioBSDFContext.mN = ioBSDFContext.mNdotV < 0 ? -ioBSDFContext.mN : ioBSDFContext.mN;
			float3 L = select(selected_r,
				reflect(-ioBSDFContext.mV, ioBSDFContext.mN),
				refract(-ioBSDFContext.mV, ioBSDFContext.mN, eta_ti));
				// refract1(ioBSDFContext.mV, cos_theta_t, eta_ti));

			// [TODO] Check this in Mitsuba
			// For transmission, radiance must be scaled to account for the solid
			// angle compression that occurs when crossing the interface.

			ioBSDFContext = GenerateContext(L, ioBSDFContext.mN, ioBSDFContext.mV, inHitContext);
			ioBSDFContext.mBSDF = select(selected_r, InstanceDatas[inHitContext.mInstanceID].mSpecularReflectance, InstanceDatas[inHitContext.mInstanceID].mSpecularTransmittance);
			ioBSDFContext.mBSDFPDF = select(selected_r, r_i, 1.0 - r_i);
			ioBSDFContext.mDeltaDistribution = true;
		}
	}

	float3 GenerateImportanceSamplingDirection(float3 inNormal, HitContext inHitContext, inout PathContext ioPathContext)
	{
		float3x3 inTangentSpace = GenerateTangentSpace(inNormal);
		[branch]
		switch (InstanceDatas[inHitContext.mInstanceID].mBSDFType)
		{
		case BSDFType::Unsupported:		// [[fallthrough]];
		case BSDFType::Diffuse:			return Lambert::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDFType::RoughConductor:	return RoughConductor::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case BSDFType::Dielectric:		return Dielectric::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		default:						return 0;
		}
	}

	void SampleDirection(HitContext inHitContext, inout BSDFContext ioBSDFContext)
	{
		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
		{
			ioBSDFContext.mBSDF = 0.0f;
			ioBSDFContext.mBSDFPDF = 0.0f;
			return;
		}

		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Mirror)
		{
			ioBSDFContext.mBSDF = 0.0f;
			ioBSDFContext.mBSDFPDF = 0.0f;
			ioBSDFContext.mDeltaDistribution = true;
			return;
		}

		[branch]
		switch (InstanceDatas[inHitContext.mInstanceID].mBSDFType)
		{
		case BSDFType::Unsupported:		// [[fallthrough]];
		case BSDFType::Diffuse:			Lambert::SampleDirection(inHitContext, ioBSDFContext); break;
		case BSDFType::RoughConductor:	RoughConductor::SampleDirection(inHitContext, ioBSDFContext); break;
		case BSDFType::Dielectric:		Dielectric::SampleDirection(inHitContext, ioBSDFContext); break;
		default:						break;
		}
	}

	void SampleBSDF(HitContext inHitContext, inout BSDFContext ioBSDFContext, inout PathContext ioPathContext)
	{
		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
		{
			ioBSDFContext.mBSDF = 0.0f;
			ioBSDFContext.mBSDFPDF = 1.0f;
			return;
		}

		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Mirror)
		{
			ioBSDFContext = GenerateContext(reflect(-ioBSDFContext.mV, ioBSDFContext.mN), ioBSDFContext.mN, ioBSDFContext.mV, inHitContext);

			ioBSDFContext.mBSDF = 1.0f;
			ioBSDFContext.mBSDFPDF = 1.0f;
			ioBSDFContext.mDeltaDistribution = true;
			return;
		}

		[branch]
		switch (InstanceDatas[inHitContext.mInstanceID].mBSDFType)
		{
		case BSDFType::Unsupported:		// [[fallthrough]];
		case BSDFType::Diffuse:			Lambert::SampleBSDF(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::RoughConductor:	RoughConductor::SampleBSDF(inHitContext, ioBSDFContext, ioPathContext); break;
		case BSDFType::Dielectric:		Dielectric::SampleBSDF(inHitContext, ioBSDFContext, ioPathContext); break;
		default:						break;
		}
	}
}