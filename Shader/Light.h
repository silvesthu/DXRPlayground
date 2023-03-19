#pragma once

#include "Shared.h"
#include "Binding.h"
#include "Common.h"

#include "RayQuery.h"

namespace LightEvaluation
{
	float GetLightSelectionPDF()
	{
		return 1.0 / mConstants.mLightCount;
	}

	void GenerateSamplingDirection(Light inLight, float3 inPositionWS, inout PathContext ioPathContext, inout float3 outDirection, inout float outPDF)
	{
		float3 vector_to_light_position = inLight.mPosition - inPositionWS;
		
		switch (inLight.mType)
		{
		case LightType::Sphere:
		{
			// UniformSampleCone / UniformConePdf
			// 
			// https://citeseerx.ist.psu.edu/doc/10.1.1.40.6561
			// https://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations#UniformConePdf

			// Position inside light is not proper handled
			// See https://www.akalin.com/sampling-visible-sphere

			float radius_squared = inLight.mHalfExtends.x * inLight.mHalfExtends.x;
			float distance_to_light_position_squared = dot(vector_to_light_position, vector_to_light_position);
			float distance_to_light_position = sqrt(distance_to_light_position_squared);

			float sin_theta_max_squared = radius_squared / distance_to_light_position_squared;
			float cos_theta_max = sqrt(1.0 - clamp(sin_theta_max_squared, 0.0, 1.0));

			// The samples are distributed uniformly over the spherical cap
			// So pdf is just one over area of it
			outPDF = 1.0 / (2.0 * MATH_PI * (1.0 - cos_theta_max));

			// Note uniform distribution is applied on cos_theta due to the form of spherical integration
			float cos_theta = lerp(cos_theta_max, 1.0, RandomFloat01(ioPathContext.mRandomState));
			float sin_theta_squared = 1.0 - cos_theta * cos_theta;
			float sin_theta = sqrt(sin_theta_squared);

			float3 direction_to_light_position = normalize(vector_to_light_position);
			float3x3 tangent_space = GenerateTangentSpace(direction_to_light_position);

			float phi = 2.0 * MATH_PI * RandomFloat01(ioPathContext.mRandomState);

			outDirection = (tangent_space[0] * cos(phi) + tangent_space[1] * sin(phi)) * sin_theta + tangent_space[2] * cos_theta;
		}
		break;
		case LightType::Rectangle:
		{
			// Seems Mitsuba3 just use uniform sampling on rectangle. See Rectangle::sample_position <- SHape::sample_direction
			// Alternatively sampling of spherical rectangles / triangles could be used (Sample with solid angle instead of surface area)

			float xi1 = RandomFloat01(ioPathContext.mRandomState);
			float xi2 = RandomFloat01(ioPathContext.mRandomState);

			vector_to_light_position += inLight.mTangent * inLight.mHalfExtends.x * (xi1 * 2.0 - 1.0);
			vector_to_light_position += inLight.mBitangent * inLight.mHalfExtends.y * (xi2 * 2.0 - 1.0);

			outDirection = normalize(vector_to_light_position);

			float distance_to_sample_position = length(vector_to_light_position);
			float surface_area = 4.0 * inLight.mHalfExtends.x * inLight.mHalfExtends.y;
			float pdf_position = 1.0 / surface_area;
			float pdf_direction = pdf_position * (distance_to_sample_position * distance_to_sample_position) / max(dot(-outDirection, inLight.mNormal), 0.0);
			outPDF = pdf_direction;
		}
		break;
		default:
			break;
		}
	}
}