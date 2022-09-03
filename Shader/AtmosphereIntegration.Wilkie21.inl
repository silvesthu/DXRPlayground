namespace AtmosphereIntegration { namespace Wilkie21 {

void GetSkyRadiance(out float3 outSkyRadiance, out float3 outTransmittanceToTop)
{
	outSkyRadiance = 0;
	outTransmittanceToTop = 1; // [TODO]

	float3 camera = PlanetRayOrigin() - PlanetCenter();
	float3 view_ray = PlanetRayDirection();
	float3 sun_direction = GetSunDirection();

	float r = length(camera);
	float rmu = dot(camera, view_ray);
	float distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + mConstants.mAtmosphere.mTopRadius * mConstants.mAtmosphere.mTopRadius);

	if (distance_to_top_atmosphere_boundary > 0.0) 
	{
		// Outer space

		// Move camera to top of atmosphere along view direction
		camera = camera + view_ray * distance_to_top_atmosphere_boundary;
		r = mConstants.mAtmosphere.mTopRadius;
		rmu += distance_to_top_atmosphere_boundary;
	}
	else if (r > mConstants.mAtmosphere.mTopRadius) 
	{
		// No hit
		return;
	}

	float mu = rmu / r;
	float mu_s = dot(camera, sun_direction) / r;
	float nu = dot(view_ray, sun_direction);
	bool ray_r_mu_intersects_ground = RayIntersectsGround(r, mu);

	AtmosphereParameters Atmosphere = GetAtmosphereParameters();

	float3 UpVector = normalize(camera);
	float3 WorldDir = view_ray;

	// Lat/Long mapping in SkyViewLut()
	float3 sideVector = normalize(cross(UpVector, WorldDir));		// assumes non parallel vectors
	float3 forwardVector = normalize(cross(sideVector, UpVector));	// aligns toward the sun light but perpendicular to up vector
	float2 lightOnPlane = float2(dot(sun_direction, forwardVector), dot(sun_direction, sideVector));
	lightOnPlane = normalize(lightOnPlane);
	float lightViewCosAngle = lightOnPlane.x;

	float2 uv;
	SkyViewLutParamsToUv(Atmosphere, ray_r_mu_intersects_ground, mu, lightViewCosAngle, r, uv);
	outSkyRadiance = Wilkie21SkyViewLutTexSRV.SampleLevel(samplerLinearClamp, uv, 0).rgb;
	
	outSkyRadiance = outSkyRadiance / kPreExposure * kSolarLM2KW; // PreExposed lm/m^2 -> kW/m^2
}

}} // namespace AtmosphereIntegration { namespace Wilkie21 {
