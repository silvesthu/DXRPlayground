// AtmosphereConstants scattering without precomputation
// Based on https://www.shadertoy.com/view/lslXDr

namespace AtmosphereIntegration { namespace Raymarch {

static const int kOutScatteringStepCount = 16;
static const int kInScatteringStepCount = 80;

// ray intersects sphere
// e = -b +/- sqrt(b^2 - c)
float2 ray_vs_sphere(float3 p, float3 dir, float r)
{
	float b = dot(p, dir);
	float c = dot(p, p) - r * r;
	
	float d = b * b - c;
	if (d < 0.0) {
		return float2(10000.0, -10000.0);
	}
	d = sqrt(d);
	
	return float2(-b - d, -b + d);
}

float optic(float3 p, float3 q, DensityProfile density_profile)
{
	float3 s = (q - p) / float(kOutScatteringStepCount);
	float3 v = p + s * 0.5;
	
	float sum = 0.0;
	for (int i = 0; i < kOutScatteringStepCount; i++) 
	{
		sum += GetProfileDensity(density_profile, length(v) - mConstants.mAtmosphere.mBottomRadius);
		v += s;
	}
	sum *= length(s);
	
	return sum;
}

float3 in_scatter(float3 o, float3 dir, float2 e, float3 l)
{	
	float3 sum_ray = 0.0;
	float3 sum_mie = 0.0;
	
	float n_ray0 = 0.0;
	float n_mie0 = 0.0;
	float n_ozone0 = 0.0;
	
	float len = (e.y - e.x) / float(kInScatteringStepCount);
	float3 s = dir * len;
	float3 v = o + dir * (e.x + len * 0.5);
	
	// [NOTE]
	// Basically same as IntegrateSingleScattering
	// Density is accumulated here instead of transmittance
	
	for (int i = 0; i < kInScatteringStepCount; i++, v += s)
	{
		float d_ray = GetProfileDensity(mConstants.mAtmosphere.mRayleighDensity, length(v) - mConstants.mAtmosphere.mBottomRadius) * len;
		float d_mie = GetProfileDensity(mConstants.mAtmosphere.mMieDensity, length(v) - mConstants.mAtmosphere.mBottomRadius) * len;
		float d_ozone = GetProfileDensity(mConstants.mAtmosphere.mOzoneDensity, length(v) - mConstants.mAtmosphere.mBottomRadius) * len;
		
		n_ray0 += d_ray;
		n_mie0 += d_mie;
		n_ozone0 += d_ozone;
		
		float2 f = ray_vs_sphere(v, l, mConstants.mAtmosphere.mTopRadius);
		float3 u = v + l * f.y;

		float n_ray1 = optic(v, u, mConstants.mAtmosphere.mRayleighDensity);
		float n_mie1 = optic(v, u, mConstants.mAtmosphere.mMieDensity);
		float n_ozone1 = optic(v, u, mConstants.mAtmosphere.mOzoneDensity);
		
		// Transmittance: Eye to v then v to Sun
		float3 att = exp(
			- (n_ray0 + n_ray1) * mConstants.mAtmosphere.mRayleighExtinction 
			- (n_mie0 + n_mie1) * mConstants.mAtmosphere.mMieExtinction
			- (n_ozone0 + n_ozone1) * mConstants.mAtmosphere.mOzoneExtinction
		);

		sum_ray += d_ray * att;
		sum_mie += d_mie * att;
	}

	float c  = dot(dir, l);
	float3 scatter =
		sum_ray * mConstants.mAtmosphere.mRayleighScattering * RayleighPhaseFunction(c) +
	 	sum_mie * mConstants.mAtmosphere.mMieScattering * MiePhaseFunction(mConstants.mAtmosphere.mMiePhaseFunctionG, c);

	return scatter;
}

void GetSkyRadiance(Ray inRayPS, out float3 outSkyRadiance, out float3 outTransmittanceToTop)
{
	outSkyRadiance = 0;
	outTransmittanceToTop = 1;

	// Position in km
	float3 camera = inRayPS.mOrigin - PlanetCenterPositionPS();
	float3 view_ray = inRayPS.mDirection;

	float2 atmosphere_hit = ray_vs_sphere(camera, view_ray, mConstants.mAtmosphere.mTopRadius);
	float2 earth_hit = ray_vs_sphere(camera, view_ray, mConstants.mAtmosphere.mBottomRadius);

	if (atmosphere_hit.x > atmosphere_hit.y)
		return; // no hit on atmosphere

	bool visualize = false;
	float2 from_to = 0;
	if (atmosphere_hit.x < 0)
	{
		// inside atmosphere
		if (earth_hit.x < 0 && earth_hit.y > 0)
		{
			// inside earth
			from_to = float2(earth_hit.y, atmosphere_hit.y);

			if (visualize)
			{
				outSkyRadiance = float3(1,0,0);
				return;
			}
		}
		else
		{
			// outside earth
			if (earth_hit.y > 0)
			{
				// hit earth
				from_to = float2(0, earth_hit.x);

				if (visualize)
				{
					outSkyRadiance = float3(1,1,0);
					return;
				}
			}
			else
			{
				// hit atmosphere
				from_to = float2(0, atmosphere_hit.y);

				if (visualize)
				{
					outSkyRadiance = float3(0,0,1);
					return;
				}
			}
		}
	}
	else
	{
		// outside atmosphere
		from_to = float2(atmosphere_hit.x, min(earth_hit.x, atmosphere_hit.y));

		if (visualize)
		{
			outSkyRadiance = float3(0,1,0);
			return;
		}
	}

	outSkyRadiance = in_scatter(camera, view_ray, from_to, GetSunDirection());
	outSkyRadiance *= mConstants.mAtmosphere.mSolarIrradiance;
}

}} // namespace AtmosphereIntegration { namespace Raymarch {