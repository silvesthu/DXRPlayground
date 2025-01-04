#pragma once

#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "Reservoir.h"

// RTXDI - minimal-sample
// For computation (RAB_LightInfo is for storage)
struct TriangleLight
{
	float3			mBase;
	float3			mEdge1;
	float3			mEdge2;
	float3			mRadiance;
	float3			mNormal;
	float			mSurfaceArea;

	RAB_LightInfo Store()
	{
		RAB_LightInfo lightInfo = (RAB_LightInfo)0;

		lightInfo.mRadiance		= Pack_R16G16B16A16_FLOAT(float4(mRadiance, 0));
		lightInfo.mCenter		= mBase + (mEdge1 + mEdge2) / 3.0;
		lightInfo.mDirection1	= ndirToOctUnorm32(normalize(mEdge1));
		lightInfo.mDirection2	= ndirToOctUnorm32(normalize(mEdge2));
		lightInfo.mScalars		= f32tof16(length(mEdge1)) | (f32tof16(length(mEdge2)) << 16);
        
		return lightInfo;
	}
};

struct LightContext
{
	bool		IsValid()				{ return mReservoir.IsValid(); }
	uint		LightIndex()			{ return mReservoir.LightIndex() ; }
	Light		Light()					{ return Lights[LightIndex()]; }
	
	float3		mL;
	float		mSolidAnglePDF;

	float		SelectionWeight()		{ return mReservoir.StochasticWeight(); }
	float		UniformSelectionPDF()	{ return 1.0 / mConstants.mLightCount; }
	Reservoir	mReservoir;
};

namespace LightEvaluation
{
	enum ContextType
	{
		Random,
		Center,
		Input,
	};

	LightContext GenerateContext(ContextType inContextType, float3 inL, uint inLightIndex, float3 inLitPositionWS, inout PathContext ioPathContext)
	{
		Light light								= Lights[inLightIndex];
		
		const float3 vector_to_light			= light.mPosition - inLitPositionWS;
		const float3 direction_to_light			= normalize(vector_to_light);
		
		LightContext light_context				= (LightContext)0;
		light_context.mL						= direction_to_light;
		light_context.mSolidAnglePDF			= 0.0;
		
		switch (light.mType)
		{
		case LightType::Sphere:
		{
			// UniformSampleCone / UniformConePdf
			// https://citeseerx.ist.psu.edu/doc/10.1.1.40.6561
			// https://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations#UniformConePdf

			// Position inside light is not proper handled
			// See https://www.akalin.com/sampling-visible-sphere

			float radius_squared				= light.mHalfExtends.x * light.mHalfExtends.x;
			float distance_to_light_position_squared = dot(vector_to_light, vector_to_light);
			float distance_to_light_position	= sqrt(distance_to_light_position_squared);

			float sin_theta_max_squared			= radius_squared / distance_to_light_position_squared;
			float cos_theta_max					= sqrt(1.0 - clamp(sin_theta_max_squared, 0.0, 1.0));

			// The samples are distributed uniformly over the spherical cap
			// So pdf is just one over area of it
			light_context.mSolidAnglePDF		= 1.0 / (2.0 * MATH_PI * (1.0 - cos_theta_max));

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
			vector_to_sample					+= light.mTangent * light.mHalfExtends.x * (xi1 * 2.0 - 1.0);
			vector_to_sample					+= light.mBitangent * light.mHalfExtends.y * (xi2 * 2.0 - 1.0);

			light_context.mL					= normalize(vector_to_sample);

			if (inContextType == ContextType::Input)
			{
				// [TODO] Validate this

				light_context.mL				= inL;
				float t							= dot(-vector_to_light, light.mNormal) / dot(-light_context.mL, light.mNormal);
				vector_to_sample				= light_context.mL * t;
			}

			float distance_to_sample_position	= length(vector_to_sample);
			float surface_area					= 4.0 * light.mHalfExtends.x * light.mHalfExtends.y;
			float pdf_position					= 1.0 / surface_area;
			float denom							= max(dot(-light_context.mL, light.mNormal), 0.0);
			float pdf_direction					= pdf_position * (distance_to_sample_position * distance_to_sample_position) / denom;

			light_context.mSolidAnglePDF		= pdf_direction;

			if (denom == 0.0)
				light_context.mSolidAnglePDF	= 0;
		}
		break;
		default:
			break;
		}

		light_context.mReservoir.mLightData		= inLightIndex | Reservoir::kLightValidBit;
		light_context.mReservoir.mCountSum		= 1;
		light_context.mReservoir.mTargetPDF		= 0.0;
		light_context.mReservoir.mWeightSum		= 0.0;
		return light_context;
	}

	LightContext SelectLight(float3 inLitPositionWS, inout PathContext ioPathContext)
	{
		LightContext selected_light_context = (LightContext)0;
		Reservoir reservoir = Reservoir::Generate();
		switch (mConstants.mLightSampleMode)
		{
		case LightSampleMode::RIS:
		{
			for (uint light_index = 0; light_index < mConstants.mLightCount; light_index++)
			{
				// [TODO] Use second-best implementation as target pdf. See RAB_GetLightSampleTargetPdfForSurface in RTXDI.
				// Currently, it is only based on luminance of light and pdf of solid angle, better to use full evaluation of rendering equation.
				LightContext light_context = LightEvaluation::GenerateContext(LightEvaluation::ContextType::Random, 0, light_index, inLitPositionWS, ioPathContext);
				light_context.mReservoir.mTargetPDF = light_context.mSolidAnglePDF <= 0.0 ? 0.0 : (RGBToLuminance(Lights[light_index].mEmission) / light_context.mSolidAnglePDF);
				float target_pdf = light_context.mReservoir.mTargetPDF;
				float candidate_pdf = light_context.UniformSelectionPDF();
				light_context.mReservoir.mWeightSum = target_pdf / candidate_pdf;			

				if (reservoir.Update(light_context.mReservoir, ioPathContext))
					selected_light_context = light_context;
			}

			selected_light_context.mReservoir = reservoir;
			return selected_light_context;
		}
		case LightSampleMode::ReSTIR:  // [passthrough]
		case LightSampleMode::Uniform: // [passthrough]
		default:
		{
			uint sample_count = mConstants.mReSTIR.mInitialSampleCount;
			if (mConstants.mLightSampleMode != LightSampleMode::ReSTIR)
				sample_count = 1;
				
			for (uint i = 0; i < min(sample_count, mConstants.mLightCount); i++)
			{
				uint light_index = min(RandomFloat01(ioPathContext.mRandomState) * mConstants.mLightCount, mConstants.mLightCount - 1);
				
				// [TODO] Use second-best implementation as target pdf. See RAB_GetLightSampleTargetPdfForSurface in RTXDI.
				// Currently, it is only based on luminance of light and pdf of sampling direction, need to add BRDF evaluation.
				LightContext light_context = LightEvaluation::GenerateContext(LightEvaluation::ContextType::Random, 0, light_index, inLitPositionWS, ioPathContext);
				light_context.mReservoir.mTargetPDF = light_context.mSolidAnglePDF <= 0.0 ? 0.0 : (RGBToLuminance(Lights[light_index].mEmission) / light_context.mSolidAnglePDF);
				float target_pdf = light_context.mReservoir.mTargetPDF;
				float candidate_pdf = light_context.UniformSelectionPDF();
				light_context.mReservoir.mWeightSum = target_pdf / candidate_pdf;

				if (reservoir.Update(light_context.mReservoir, ioPathContext))
					selected_light_context = light_context;
			}

			selected_light_context.mReservoir = reservoir;
			return selected_light_context;	
		}
		}
	}
}