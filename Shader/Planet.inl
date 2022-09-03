
float3 PlanetRayOrigin()
{
	float3 origin = sGetWorldRayOrigin() * mConstants.mAtmosphere.mSceneScale;
    origin.y = max(origin.y, 0);		// Keep observer position above ground
    return origin;
}

float3 PlanetRayDirection()
{
	return sGetWorldRayDirection();
}

float3 PlanetRayHitPosition()
{
	float3 position = (sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent()) * mConstants.mAtmosphere.mSceneScale;
    position.y = max(position.y, 0);	// Keep sampling position above ground
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