#pragma once

#include "Common.h"

struct Reservoir
{
	static const uint kLightValidBit			= 0x80000000;
	static const uint kLightIndexMask			= 0x7FFFFFFF;
	
	uint			mLightData;                 // r.y
	float			mTotalWeight;				// r.w_sum
	
	uint			mSampleCount;				// r.M
	float			mTargetPDF;					// \hat{p_{q}}(r.y)

	bool			IsValid()					{ return mLightData != 0; }
	uint			LightIndex()				{ return mLightData & kLightIndexMask; }

	bool			Update(Reservoir inReservoir, inout PathContext ioPathContext)
	{
		mTotalWeight							+= inReservoir.mTotalWeight;
		mSampleCount							+= inReservoir.mSampleCount;
		
		if (RandomFloat01(ioPathContext.mRandomState) * mTotalWeight < inReservoir.mTotalWeight)
		{
			mLightData							= inReservoir.mLightData;
			return true;
		}

		return false;
	}

	static Reservoir Generate()
	{
		Reservoir reservoir;
		reservoir.mLightData					= 0;
		reservoir.mTotalWeight					= 0.0;
		reservoir.mSampleCount					= 1;
		return reservoir;
	}

	static float4 Pack(Reservoir inReservoir)
	{
		float4 raw_reservoir					= 0;
		raw_reservoir.x							= asfloat(inReservoir.mLightData);
		raw_reservoir.y							= inReservoir.mTotalWeight;
		raw_reservoir.z							= 0.0;
		raw_reservoir.w							= 0.0;
		return raw_reservoir;
	}

	static Reservoir Unpack(float4 inRawReservoir)
	{
		Reservoir reservoir;
		reservoir.mLightData					= asuint(inRawReservoir.x);
		reservoir.mTotalWeight					= inRawReservoir.y;
		return reservoir;
	}
};
