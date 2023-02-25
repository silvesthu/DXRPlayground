#pragma once
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

#include "RayQuery.h"

namespace MaterialEvaluation
{
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

		void SampleDirection(HitContext inHitContext, inout MaterialContext ioMaterialContext)
		{
			ioMaterialContext.mBSDF = MaterialEvaluation::Source::Albedo(inHitContext) / MATH_PI;
			ioMaterialContext.mBSDFPDF = ioMaterialContext.mNdotL / MATH_PI;
		}

		void SampleBSDF(HitContext inHitContext, inout MaterialContext ioMaterialContext)
		{
			ioMaterialContext.mBSDF = MaterialEvaluation::Source::Albedo(inHitContext) / MATH_PI;
			ioMaterialContext.mBSDFPDF = ioMaterialContext.mNdotL / MATH_PI;
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

		void SampleDirection(HitContext inHitContext, inout MaterialContext ioMaterialContext)
		{
			float D = D_GGX(ioMaterialContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float G = G_SmithGGX(ioMaterialContext.mNdotL, ioMaterialContext.mNdotV, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float3 F = F_Conductor_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta, InstanceDatas[inHitContext.mInstanceID].mK, ioMaterialContext.mHdotV) * InstanceDatas[inHitContext.mInstanceID].mReflectance;

			ioMaterialContext.mBSDF = D * G * F / (4.0f * ioMaterialContext.mNdotV * ioMaterialContext.mNdotL);
			ioMaterialContext.mBSDFPDF = (D * ioMaterialContext.mNdotH) / (4.0f * ioMaterialContext.mHdotL);
		}

		void SampleBSDF(HitContext inHitContext, inout MaterialContext ioMaterialContext)
		{
			float D = D_GGX(ioMaterialContext.mNdotH, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float G = G_SmithGGX(ioMaterialContext.mNdotL, ioMaterialContext.mNdotV, InstanceDatas[inHitContext.mInstanceID].mRoughnessAlpha);
			float3 F = F_Conductor_Mitsuba(InstanceDatas[inHitContext.mInstanceID].mEta, InstanceDatas[inHitContext.mInstanceID].mK, ioMaterialContext.mHdotV) * InstanceDatas[inHitContext.mInstanceID].mReflectance;

			// [NOTE] Mitsuba3 use Non-height-correlated Smith-GGX, see smith_g1 use in roughconductor.cpp
			// [NOTE] Mitsuba3 use Visible normal sampling by default which affects both D and pdf of D

			ioMaterialContext.mBSDF = D * G * F / (4.0f * ioMaterialContext.mNdotV * ioMaterialContext.mNdotL);
			// ioMaterialContext.mBSDF = D * (G * ioMaterialContext.mHdotV) * F / (ioMaterialContext.mNdotV * ioMaterialContext.mNdotH); // Same as in roughconductor.cpp but not match?
			ioMaterialContext.mBSDFPDF = (D * ioMaterialContext.mNdotH) / (4.0f * ioMaterialContext.mHdotL);
		}
	}

	float3 GenerateImportanceSamplingDirection(float3 inNormal, HitContext inHitContext, inout PathContext ioPathContext)
	{
		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Mirror)
			return reflect(inHitContext.mRayDirectionWS, inNormal);

		float3x3 inTangentSpace = GenerateTangentSpace(inNormal);
		[branch]
		switch (InstanceDatas[inHitContext.mInstanceID].mMaterialType)
		{
		case MaterialType::Diffuse:			return Lambert::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		case MaterialType::RoughConductor:	return RoughConductor::GenerateImportanceSamplingDirection(inTangentSpace, inHitContext, ioPathContext);
		default:							return 0;
		}
	}

	void SampleDirection(HitContext inHitContext, inout MaterialContext ioMaterialContext)
	{
		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
		{
			ioMaterialContext.mBSDF = 0.0f;
			ioMaterialContext.mBSDFPDF = 1.0f;
			return;
		}

		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Mirror)
		{
			ioMaterialContext.mBSDF = 1.0f;
			ioMaterialContext.mBSDFPDF = 1.0f;
			return;
		}

		if (!ioMaterialContext.IsValid())
		{
			ioMaterialContext.mBSDF = 0.0f;
			ioMaterialContext.mBSDFPDF = 1.0f;
			return;
		}

		[branch]
		switch (InstanceDatas[inHitContext.mInstanceID].mMaterialType)
		{
		case MaterialType::Diffuse:			Lambert::SampleDirection(inHitContext, ioMaterialContext); break;
		case MaterialType::RoughConductor:	RoughConductor::SampleDirection(inHitContext, ioMaterialContext); break;
		default:							break;
		}
	}

	void SampleBSDF(HitContext inHitContext, inout MaterialContext ioMaterialContext)
	{
		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Barycentrics)
		{
			ioMaterialContext.mBSDF = 0.0f;
			ioMaterialContext.mBSDFPDF = 1.0f;
			return;
		}

		if (mConstants.mDebugInstanceIndex == inHitContext.mInstanceID && mConstants.mDebugInstanceMode == DebugInstanceMode::Mirror)
		{
			ioMaterialContext.mBSDF = 1.0f;
			ioMaterialContext.mBSDFPDF = 1.0f;
			return;
		}

		if (!ioMaterialContext.IsValid())
		{
			ioMaterialContext.mBSDF = 0.0f;
			ioMaterialContext.mBSDFPDF = 1.0f;
			return;
		}

		[branch]
		switch (InstanceDatas[inHitContext.mInstanceID].mMaterialType)
		{
		case MaterialType::Diffuse:			Lambert::SampleBSDF(inHitContext, ioMaterialContext); break;
		case MaterialType::RoughConductor:	RoughConductor::SampleBSDF(inHitContext, ioMaterialContext); break;
		default:							break;
		}
	}

	MaterialContext GenerateContext(float3 inLight, float3 inNormal, float3 inView, HitContext inHitContext)
	{
		MaterialContext material_context;

		material_context.mL = inLight;
		material_context.mN = inNormal;
		material_context.mV = inView;
		material_context.mH = normalize((inLight + inView) / 2.0);

		material_context.mNdotH = dot(material_context.mN, material_context.mH);
		material_context.mNdotV = dot(material_context.mN, material_context.mV);
		material_context.mNdotL = dot(material_context.mN, material_context.mL);
		material_context.mHdotV = dot(material_context.mH, material_context.mV);
		material_context.mHdotL = dot(material_context.mH, material_context.mL);

		return material_context;
	}
}