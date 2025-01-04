#pragma once

#include "Common.h"

struct Reservoir
{
	static const uint kLightValidBit			= 0x80000000;
	static const uint kLightIndexMask			= 0x7FFFFFFF;
	
	uint			mLightData;                 																		// r.y                  // Selected sample
	float			mTargetPDF;																							// \hat{p_{q}}(r.y)		// Target PDF of selected sample
	
	float			mWeightSum;																							// r.w_sum				// Sum of processed sample weight	                                            																		
	uint			mCountSum;																							// r.M					// Sum of processed sample count

	float			StochasticWeight()																					// r.W                  // Stochastic weight, expected value is 1/p(y). Eq(6)
	{
		if (mTargetPDF == 0.0)
			return 0.0;
		
		return (1.0 / mTargetPDF) * (1.0 / mCountSum * mWeightSum);
	}

	bool			IsValid()					{ return mLightData != 0; }
	uint			LightIndex()				{ return mLightData & kLightIndexMask; }

	bool			Update(Reservoir inReservoir, inout PathContext ioPathContext)
	{
		mWeightSum								+= inReservoir.mWeightSum;
		mCountSum								+= inReservoir.mCountSum;
		
		if (RandomFloat01(ioPathContext.mRandomState) * mWeightSum < inReservoir.mWeightSum)
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
		reservoir.mLightData					= 0;
		reservoir.mWeightSum					= 0.0;
		reservoir.mCountSum					= 0;
		reservoir.mTargetPDF					= 0.0;
		return reservoir;
	}

	static float4 Pack(Reservoir inReservoir)
	{
		float4 raw_reservoir					= 0;
		raw_reservoir.x							= asfloat(inReservoir.mLightData);
		raw_reservoir.y							= inReservoir.mWeightSum;
		raw_reservoir.z							= 0.0;
		raw_reservoir.w							= 0.0;
		return raw_reservoir;
	}

	static Reservoir Unpack(float4 inRawReservoir)
	{
		Reservoir reservoir;
		reservoir.mLightData					= asuint(inRawReservoir.x);
		reservoir.mWeightSum					= inRawReservoir.y;
		return reservoir;
	}
};
