#pragma once
#include "Common.h"
#include "Context.h"

// Following formulation in [Wyman2023] https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf
struct Reservoir
{
	static const uint kLightValidBit			= 0x80000000;
	static const uint kLightIndexMask			= 0x7FFFFFFF;
	
	uint			mLightData;                 // Selected sample, X
	uint			mUV;

	float			mTargetFunction;			// Target function (unnormalized), \hat{p}(x). TargetPdf in RTXDI
	float			mContributionWeight;		// Unbiased contribution weight (UCW), W_X. Not Unbiased in this implementation as visibility is not evaluated for all samples

	float			mWeightSum;					// Sum of resampling weight, \sum w_i

	float			mM;							// Sample count, M

	bool			IsValid()					{ return mLightData != 0; }
	uint			LightIndex()				{ return (mLightData & kLightIndexMask); }

	static Reservoir Generate()
	{
		Reservoir reservoir;
		reservoir.mLightData					= 0;
		reservoir.mUV							= 0;
		reservoir.mTargetFunction				= 0.0f;
		reservoir.mContributionWeight			= 0.0f;
		reservoir.mWeightSum					= 0.0f;
		reservoir.mM							= 0.0f;
		return reservoir;
	}

	static Reservoir FromLight(LightContext inLightContex, float inTargetFunction, float inContributionWeight)
	{
		Reservoir reservoir;
		reservoir.mLightData					= inLightContex.mLightIndex | Reservoir::kLightValidBit;
		reservoir.mUV							= uint(saturate(inLightContex.mUV.x) * 0xffff) | (uint(saturate(inLightContex.mUV.y) * 0xffff) << 16);
		reservoir.mTargetFunction				= inTargetFunction;
		reservoir.mContributionWeight			= inContributionWeight;
		reservoir.mWeightSum					= 0.0f;
		reservoir.mM							= 0.0f;
		return reservoir;
	}

	// Add sample to reservoir, see RTXDI_StreamSample, RTXDI_CombineDIReservoirs in RTXDI
	bool			Stream(Reservoir inReservoir, float inMISWeight, inout uint ioRandomState)
	{
		// [Wyman2023]
		// Resampling Weight:	w_i = m_i * \hat{p}(X_i) / p(X_i)
		// MIS Weight:			m_i
		// Target Function:		\hat{p}(X_i)
		// Source PDF:			p(X_i)
		float resampling_weight					= inMISWeight * inReservoir.mTargetFunction * inReservoir.mContributionWeight;
		mWeightSum								+= resampling_weight;

		mM										+= 1;

		bool select_sample						= RandomFloat01(ioRandomState) * mWeightSum < resampling_weight;
		if (select_sample)
		{
			mLightData							= inReservoir.mLightData;
			mUV									= inReservoir.mUV;
			mTargetFunction						= inReservoir.mTargetFunction;
		}

		return select_sample;
	}

	// See also RTXDI_FinalizeResampling in RTXDI
	void			ComputeContributionWeight()
	{
		// Unbiased contribution weight (UCW), W_X = \sum w_i / \hat{p}(X) in [Wyman2023]
		mContributionWeight						= (mTargetFunction == 0.0) ? 0.0 : mWeightSum / mTargetFunction;
	}

	uint4 Pack()
	{
		uint4 packed							= 0;
		packed.x								= mLightData;
		packed.y								= asuint(mContributionWeight);
		packed.z								= mUV;
		packed.w								= 0;
		return packed;
	}

	void Unpack(uint4 inPacked)
	{
		mLightData								= inPacked.x;
		mContributionWeight						= asfloat(inPacked.y);
		mUV										= inPacked.z;
	}
};
