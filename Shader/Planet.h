// PlanetSpace (PS)
#pragma once
#include "Context.h"

float3 PositionWS2PS(float3 inPositionWS)
{
	float3 position = inPositionWS * mConstants.mAtmosphere.mSceneScale;
    return position;
}

float3 DirectionWS2PS(float3 inDirectionWS)
{
	return inDirectionWS;
}

Ray RayWS2PS(Ray inRayWS)
{
	Ray ray;
	ray.mOrigin = PositionWS2PS(inRayWS.mOrigin);
	ray.mDirection = DirectionWS2PS(inRayWS.mDirection);
	ray.mTCurrent = inRayWS.mTCurrent;
	return ray;
}

float3 PlanetCenterPositionPS()
{
	return float3(0, -mConstants.mAtmosphere.mBottomRadius, 0);
}

float PlanetRadiusPS()
{
	return mConstants.mAtmosphere.mBottomRadius;
}