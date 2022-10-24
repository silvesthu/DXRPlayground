#pragma once

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

static uint sInstanceIndex;
uint sGetInstanceIndex()
{
#ifdef ENABLE_RAY_QUERY
	return sInstanceIndex;
#else
	return InstanceIndex();
#endif // ENABLE_RAY_QUERY
}

static uint sPrimitiveIndex;
uint sGetPrimitiveIndex()
{
#ifdef ENABLE_RAY_QUERY
	return sPrimitiveIndex;
#else
	return PrimitiveIndex();
#endif // ENABLE_RAY_QUERY
}

static uint sGeometryIndex;
uint sGetGeometryIndex()
{
#ifdef ENABLE_RAY_QUERY
	return sGeometryIndex;
#else
	return GeometryIndex();
#endif // ENABLE_RAY_QUERY
}

static uint sInstanceID;
uint sGetInstanceID()
{
#ifdef ENABLE_RAY_QUERY
	return sInstanceID;
#else
	return InstanceID();
#endif // ENABLE_RAY_QUERY
}

float3 sGetWorldRayHitPosition()
{
	return sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent();
}