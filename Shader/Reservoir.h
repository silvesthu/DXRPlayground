#pragma once
#include "Common.h"
#include "Context.h"

struct Reservoir
{
	static const uint kLightValidBit			= 0x80000000;
	static const uint kLightIndexMask			= 0x7FFFFFFF;
	
	uint			mLightData;                 // r.y                  // Selected sample
	uint			mUV;

	float			mTargetPDF;					// \hat{p_{q}}(r.y)		// Target PDF of selected sample
	
	float			mWeightSum;					// r.w_sum				// Sum of processed sample weight	                                            																		
	float			mM;							// r.M					// Sum of processed sample count

	float			StochasticWeight()			// r.W                  // Stochastic weight, expected value is 1/p(y). Eq(6)
	{
		if (mTargetPDF == 0.0)
			return 0.0;
		
		return (1.0 / mTargetPDF) * (1.0 / mM * mWeightSum);
	}

	bool			IsValid()					{ return (mLightData & kLightValidBit) != 0; }
	uint			LightIndex()				{ return (mLightData & kLightIndexMask); }

	bool			Stream(LightContext inLightContex, float inTargetPDF, float inCandidatePDF, inout uint ioRandomState)
	{
		float ris_weight						= inTargetPDF / inCandidatePDF;
		mM										+= 1;
		mWeightSum								+= ris_weight;

		bool select_sample						= RandomFloat01(ioRandomState) * mWeightSum < ris_weight;
		if (select_sample)
		{
			mLightData							= inLightContex.mLightIndex | Reservoir::kLightValidBit;
			mUV									= uint(saturate(inLightContex.mUV.x) * 0xffff) | (uint(saturate(inLightContex.mUV.y) * 0xffff) << 16);
			mTargetPDF							= inTargetPDF;
		}

		return select_sample;
	}

	bool			Combine(Reservoir inReservoir, inout uint ioRandomState)
	{
		mWeightSum								+= inReservoir.mWeightSum;
		mM										+= inReservoir.mM;
		
		if (RandomFloat01(ioRandomState) * mWeightSum < inReservoir.mWeightSum)
		{
			mLightData							= inReservoir.mLightData;
			mTargetPDF							= inReservoir.mTargetPDF;
			return true;
		}

		return false;
	}

	static Reservoir Generate()
	{
		Reservoir reservoir;
		reservoir.mLightData	= 0;
		reservoir.mUV = 0;
		reservoir.mWeightSum	= 0.0f;
		reservoir.mM			= 0.0f;
		reservoir.mTargetPDF	= 0.0f;
		return reservoir;
	}

	static float4 Pack(Reservoir inReservoir)
	{
		float4 raw_reservoir	= 0;
		raw_reservoir.x			= asfloat(inReservoir.mLightData);
		raw_reservoir.y			= inReservoir.mWeightSum;
		raw_reservoir.z			= 0.0;
		raw_reservoir.w			= 0.0;
		return raw_reservoir;
	}

	static Reservoir Unpack(float4 inRawReservoir)
	{
		Reservoir reservoir;
		reservoir.mLightData	= asuint(inRawReservoir.x);
		reservoir.mWeightSum	= inRawReservoir.y;
		return reservoir;
	}
};
