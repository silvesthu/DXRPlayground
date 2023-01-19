
float3 PlanetRayOrigin()
{
	float3 origin = sGetWorldRayOrigin() * mConstants.mAtmosphere.mSceneScale;
    return origin;
}

float3 PlanetRayDirection()
{
	return sGetWorldRayDirection();
}

float3 PlanetRayHitPosition()
{
	float3 position = (sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent()) * mConstants.mAtmosphere.mSceneScale;
    return position;
}

float3 PlanetCenter()
{
	return float3(0, -mConstants.mAtmosphere.mBottomRadius, 0);
}

float PlanetRadius()
{
	return mConstants.mAtmosphere.mBottomRadius;
}