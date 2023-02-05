#pragma once

#include "Constant.h"
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

#include "RayQuery.h"

namespace MaterialEvaluation
{
	struct Source
	{
		static float3 Albedo(HitInfo inHitInfo)
		{
			uint albedo_texture_index = InstanceDatas[sGetInstanceID()].mAlbedoTextureIndex;
			[branch]
			if (albedo_texture_index != (uint)ViewDescriptorIndex::Invalid)
			{
				Texture2D<float4> albedo_texture = ResourceDescriptorHeap[albedo_texture_index];
				return albedo_texture.SampleLevel(BilinearWrapSampler, inHitInfo.mUV, 0).rgb;
			}

			return InstanceDatas[sGetInstanceID()].mAlbedo;
		}
	};

	struct Context
	{
		static Context Generate(float3 inL, float3 inN)
		{
			Context context;

			context.L = inL;
			context.N = inN;
			context.V = -sGetWorldRayDirection();
			context.H = normalize((context.L + context.V) / 2.0);

			context.NdotH = dot(context.N, context.H);
			context.NdotV = dot(context.N, context.V);
			context.NdotL = dot(context.N, context.L);
			context.HdotV = dot(context.H, context.V);
			context.HdotL = dot(context.H, context.L);

			return context;
		}

		float3 L;
		float3 N;
		float3 V;
		float3 H;

		float NdotH;
		float NdotV;
		float NdotL;
		float HdotV;
		float HdotL;
	};

	namespace Lambert
	{
		float3 GenerateSample(float3x3 inTangentSpace, inout RayPayload payload)
		{
			float3 direction = RandomCosineDirection(payload.mRandomState);
			return normalize(direction.x * inTangentSpace[0] + direction.y * inTangentSpace[1] + direction.z * inTangentSpace[2]);
		}

		float ComputePDF(Context inContext)
		{
			if (inContext.NdotL <= 0)
				return 0;

			return inContext.NdotL / MATH_PI;
		}

		void Evaluate(Context inContext, inout HitInfo ioHitInfo)
		{
			ioHitInfo.mReflectionDirection		= inContext.L;
			ioHitInfo.mBSDF						= MaterialEvaluation::Source::Albedo(ioHitInfo) / MATH_PI;
			ioHitInfo.mNdotL					= inContext.NdotL;
			ioHitInfo.mSamplingPDF				= ComputePDF(inContext);
			ioHitInfo.mDone						= false;
		}
	};

	namespace RoughConductor
	{
		float3 GenerateSample(float3x3 inTangentSpace, inout RayPayload payload)
		{
			float a = InstanceDatas[sGetInstanceID()].mRoughnessAlpha;
			float a2 = a * a;

			// Microfacet
			float3 H; // Microfacet normal (Half-vector)
			{
				float e0 = RandomFloat01(payload.mRandomState);
				float e1 = RandomFloat01(payload.mRandomState);

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

		float ComputePDF(Context inContext)
		{
			if (inContext.NdotL <= 0 || inContext.NdotH <= 0 || inContext.HdotL <= 0)
				return 0;

			return inContext.NdotH / (4.0f * inContext.HdotL);
		}

		void Evaluate(Context inContext, inout HitInfo ioHitInfo)
		{
			float a							= InstanceDatas[sGetInstanceID()].mRoughnessAlpha;
			float a2						= a * a;

			float G							= G_SmithGGX(inContext.NdotL, inContext.NdotV, a2);
			float3 F						= F_Conductor_Mitsuba(InstanceDatas[sGetInstanceID()].mEta, InstanceDatas[sGetInstanceID()].mK, inContext.HdotV);
			F								*= InstanceDatas[sGetInstanceID()].mReflectance;

			ioHitInfo.mReflectionDirection	= inContext.L;
			ioHitInfo.mBSDF					= G * F / (4.0f * inContext.NdotV * inContext.NdotL);
			ioHitInfo.mNdotL				= inContext.NdotL;
			ioHitInfo.mSamplingPDF			= ComputePDF(inContext);
			ioHitInfo.mDone					= false;
		}
	}

	float3 GenerateSample(float3 inNormal, inout RayPayload ioPayload)
	{
		float3x3 inTangentSpace = GenerateTangentSpace(inNormal);
		[branch]
		switch (InstanceDatas[sGetInstanceID()].mMaterialType)
		{
		case MaterialType::Diffuse:			return Lambert::GenerateSample(inTangentSpace, ioPayload);
		case MaterialType::RoughConductor:	return RoughConductor::GenerateSample(inTangentSpace, ioPayload);
		default:							return 0;
		}
	}

	float ComputePDF(float3 inLight, float3 inNormal)
	{
		Context context = Context::Generate(inLight, inNormal);
		[branch]
		switch (InstanceDatas[sGetInstanceID()].mMaterialType)
		{
		case MaterialType::Diffuse:			return Lambert::ComputePDF(context);
		case MaterialType::RoughConductor:	return RoughConductor::ComputePDF(context);
		default:							return 0;
		}
	}

	void Evaluate(float3 inLight, float3 inNormal, inout HitInfo ioHitInfo)
	{
		Context context = Context::Generate(inLight, inNormal);
		[branch]
		switch (InstanceDatas[sGetInstanceID()].mMaterialType)
		{
		case MaterialType::Diffuse:			Lambert::Evaluate(context, ioHitInfo); break;
		case MaterialType::RoughConductor:	RoughConductor::Evaluate(context, ioHitInfo); break;
		default:							break;
		}
	}
}