#pragma once

#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Reservoir.h"

struct LightContext
{
	float3	mL;
	float	mLPDF;
};

namespace LightEvaluation
{
	enum ContextType
	{
		Random,
		Center,
		Input,
	};

	LightContext GenerateContext(ContextType inContextType, float3 inL, Light inLight, float3 inLitPositionWS, inout PathContext ioPathContext)
	{
		const float3 vector_to_light			= inLight.mPosition - inLitPositionWS;
		const float3 direction_to_light			= normalize(vector_to_light);

		LightContext light_context;
		light_context.mL						= direction_to_light;
		light_context.mLPDF						= 0.0;
		
		switch (inLight.mType)
		{
		case LightType::Sphere:
		{
			// UniformSampleCone / UniformConePdf
			// https://citeseerx.ist.psu.edu/doc/10.1.1.40.6561
			// https://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations#UniformConePdf

			// Position inside light is not proper handled
			// See https://www.akalin.com/sampling-visible-sphere

			float radius_squared				= inLight.mHalfExtends.x * inLight.mHalfExtends.x;
			float distance_to_light_position_squared = dot(vector_to_light, vector_to_light);
			float distance_to_light_position	= sqrt(distance_to_light_position_squared);

			float sin_theta_max_squared			= radius_squared / distance_to_light_position_squared;
			float cos_theta_max					= sqrt(1.0 - clamp(sin_theta_max_squared, 0.0, 1.0));

			// The samples are distributed uniformly over the spherical cap
			// So pdf is just one over area of it
			light_context.mLPDF					= 1.0 / (2.0 * MATH_PI * (1.0 - cos_theta_max));

			float xi1							= RandomFloat01(ioPathContext.mRandomState);
			float xi2							= RandomFloat01(ioPathContext.mRandomState);

			if (inContextType == ContextType::Center)
			{
				xi1								= 1.0;
				xi2								= 1.0;
			}

			// Note uniform distribution is applied on cos_theta due to the form of spherical integration
			float cos_theta						= lerp(cos_theta_max, 1.0, xi1);
			float sin_theta_squared				= 1.0 - cos_theta * cos_theta;
			float sin_theta						= sqrt(sin_theta_squared);

			float3x3 tangent_space				= GenerateTangentSpace(direction_to_light);

			float phi							= 2.0 * MATH_PI * xi2;

			light_context.mL					= (tangent_space[0] * cos(phi) + tangent_space[1] * sin(phi)) * sin_theta + tangent_space[2] * cos_theta;

			if (inContextType == ContextType::Input)
			{
				light_context.mL				= inL;
			}
		}
		break;
		case LightType::Rectangle:
		{
			// Seems Mitsuba3 just use uniform sampling on rectangle. See Rectangle::sample_position <- Shape::sample_direction
			// Alternatively sampling of spherical rectangles / triangles could be used (Sample with solid angle instead of surface area)

			float xi1							= RandomFloat01(ioPathContext.mRandomState);
			float xi2							= RandomFloat01(ioPathContext.mRandomState);

			if (inContextType == ContextType::Center)
			{
				xi1								= 0.5;
				xi2								= 0.5;
			}

			float3 vector_to_sample				= vector_to_light;
			vector_to_sample					+= inLight.mTangent * inLight.mHalfExtends.x * (xi1 * 2.0 - 1.0);
			vector_to_sample					+= inLight.mBitangent * inLight.mHalfExtends.y * (xi2 * 2.0 - 1.0);

			light_context.mL					= normalize(vector_to_sample);

			if (inContextType == ContextType::Input)
			{
				// [TODO] Validate this

				light_context.mL				= inL;
				float t							= dot(-vector_to_light, inLight.mNormal) / dot(-light_context.mL, inLight.mNormal);
				vector_to_sample				= light_context.mL * t;
			}

			float distance_to_sample_position	= length(vector_to_sample);
			float surface_area					= 4.0 * inLight.mHalfExtends.x * inLight.mHalfExtends.y;
			float pdf_position					= 1.0 / surface_area;
			float denom							= max(dot(-light_context.mL, inLight.mNormal), 0.0);
			float pdf_direction					= pdf_position * (distance_to_sample_position * distance_to_sample_position) / denom;

			light_context.mLPDF					= pdf_direction;

			if (denom == 0.0)
				light_context.mLPDF				= 0;
		}
		break;
		default:
			break;
		}

		return light_context;
	}

	float CalculateWeight(uint inLightIndex, float3 inLitPositionWS, inout PathContext ioPathContext)
	{
		// [NOTE] Use uniform weight here is effectively same as LightSampleMode::Uniform
		// [NOTE] minimal-sample in RTXDI merge light reservoir and brdf reservoir for initial sample

		// [TODO] Also weight on BSDF

		LightContext light_context = LightEvaluation::GenerateContext(LightEvaluation::ContextType::Random, 0, Lights[inLightIndex], inLitPositionWS, ioPathContext);
		return light_context.mLPDF <= 0.0 ? 0.0 : RGBToLuminance(Lights[inLightIndex].mEmission) / light_context.mLPDF;
	}

	uint SelectLight(float3 inLitPositionWS, inout PathContext ioPathContext)
	{
		switch (mConstants.mLightSampleMode)
		{
		case LightSampleMode::RIS:
		{
			Reservoir reservoir = Reservoir::Generate();
			for (uint light_index = 0; light_index < mConstants.mLightCount; light_index++)
			{
				float weight = CalculateWeight(light_index, inLitPositionWS, ioPathContext);
				reservoir.Update(light_index, weight, ioPathContext);
			}
			return reservoir.mLightIndex;
		}
		case LightSampleMode::Uniform: // [passthrough]
		default: return min(RandomFloat01(ioPathContext.mRandomState) * mConstants.mLightCount, mConstants.mLightCount - 1);
		}
	}

	float SelectLightPDF(uint inLightIndex, float3 inLitPositionWS, inout PathContext ioPathContext)
	{
		switch (mConstants.mLightSampleMode)
		{
		case LightSampleMode::RIS:
		{
			float selected_light_weight = 0.0;
			Reservoir reservoir = Reservoir::Generate();
			for (uint light_index = 0; light_index < mConstants.mLightCount; light_index++)
			{
				float weight = CalculateWeight(light_index, inLitPositionWS, ioPathContext);

				if (mConstants.mPixelDebugLightIndex == light_index)
					DebugValue(PixelDebugMode::RISWeight, ioPathContext.mRecursionDepth, weight);

				if (inLightIndex == light_index)
					selected_light_weight = weight;

				reservoir.Update(light_index, weight, ioPathContext);
			}
			return selected_light_weight / reservoir.mTotalWeight;
		}
		case LightSampleMode::Uniform: // [passthrough]
		default: return 1.0 / mConstants.mLightCount;
		}
	}
}