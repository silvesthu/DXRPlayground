#pragma once

#include "Shared.h"
#include "Binding.h"
#include "Common.h"

struct Reservoir
{
	uint			mLightIndex;
	float			mTotalWeight;

	bool			Update(Reservoir inReservoir, inout PathContext ioPathContext)
	{
		mTotalWeight							+= inReservoir.mTotalWeight;
		
		float threshold							= mTotalWeight == 0.0 ? 1.0 : (inReservoir.mTotalWeight / mTotalWeight); // in case 0.0 / 0.0, use division only for clarity. Otherwise, better to use xi * mTotalWeight < inWeight
		float xi								= RandomFloat01(ioPathContext.mRandomState);
		if (xi < threshold)
		{
			mLightIndex							= inReservoir.mLightIndex;
			return true;
		}

		return false;
	}

	static Reservoir Generate()
	{
		Reservoir reservoir;
		reservoir.mLightIndex					= 0;
		reservoir.mTotalWeight					= 0.0;
		return reservoir;
	}

	static float4 Pack(Reservoir inReservoir)
	{
		float4 raw_reservoir					= 0;
		raw_reservoir.x							= asfloat(inReservoir.mLightIndex);
		raw_reservoir.y							= inReservoir.mTotalWeight;
		raw_reservoir.z							= 0.0;
		raw_reservoir.w							= 0.0;
		return raw_reservoir;
	}

	static Reservoir Unpack(float4 inRawReservoir)
	{
		Reservoir reservoir;
		reservoir.mLightIndex					= asuint(inRawReservoir.x);
		reservoir.mTotalWeight					= inRawReservoir.y;
		return reservoir;
	}
};
