#pragma once

struct PixelContext
{
	uint3	mPixelIndex;
	uint3	mPixelTotal;

	bool	mOutputDepth;
	float	mDepth;
};

// [TODO] Remove these
// Adapters for RayQuery

#ifndef SHADER_PROFILE_LIB
#define ENABLE_RAY_QUERY
#endif // SHADER_PROFILE_LIB

static float3 sWorldRayOrigin;
float3 sGetWorldRayOrigin()
{
#ifdef ENABLE_RAY_QUERY
	return sWorldRayOrigin;
#else
	return WorldRayOrigin();
#endif // ENABLE_RAY_QUERY
}

static float3 sWorldRayDirection;
float3 sGetWorldRayDirection()
{
#ifdef ENABLE_RAY_QUERY
	return sWorldRayDirection;
#else
	return WorldRayDirection();
#endif // ENABLE_RAY_QUERY
}

static float sRayTCurrent;
float sGetRayTCurrent()
{
#ifdef ENABLE_RAY_QUERY
	return sRayTCurrent;
#else
	return RayTCurrent();
#endif // ENABLE_RAY_QUERY
}
