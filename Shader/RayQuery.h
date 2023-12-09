#pragma once

struct PixelContext
{
	uint3	mPixelIndex;
	uint3	mPixelTotal;
};

// [TODO] Remove these
// Adapters for RayQuery

#ifndef SHADER_PROFILE_LIB
#define ENABLE_RAY_QUERY
#endif // SHADER_PROFILE_LIB

static uint3 sDispatchRaysIndex;
uint3 sGetDispatchRaysIndex()
{
#ifdef ENABLE_RAY_QUERY
	return sDispatchRaysIndex;
#else
	return DispatchRaysIndex();
#endif // ENABLE_RAY_QUERY
}

static uint3 sDispatchRaysDimensions;
uint3 sGetDispatchRaysDimensions()
{
#ifdef ENABLE_RAY_QUERY
	return sDispatchRaysDimensions;
#else
	return DispatchRaysDimensions();
#endif // ENABLE_RAY_QUERY
}

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
