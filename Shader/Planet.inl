
float3 PlanetRayOrigin()
{
	float3 origin = sGetWorldRayOrigin() * mPerFrameConstants.mAtmosphere.mSceneScale;
    origin.y = max(origin.y, 0);		// Keep observer position above ground
    return origin;
}

float3 PlanetRayDirection()
{
	return sGetWorldRayDirection();
}

float3 PlanetRayHitPosition()
{
	float3 position = (sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent()) * mPerFrameConstants.mAtmosphere.mSceneScale;
    position.y = max(position.y, 0);	// Keep sampling position above ground
    return position;
}

float3 PlanetCenter()
{
	return float3(0, -mPerFrameConstants.mAtmosphere.mBottomRadius, 0);
}

float PlanetRadius()
{
	return mPerFrameConstants.mAtmosphere.mBottomRadius;
}