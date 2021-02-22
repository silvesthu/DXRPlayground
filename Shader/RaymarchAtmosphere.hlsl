// Atmosphere scattering without precomputation
// Based on https://www.shadertoy.com/view/lslXDr

#define NUM_OUT_SCATTER 16
#define NUM_IN_SCATTER 80

// ray intersects sphere
// e = -b +/- sqrt( b^2 - c )
float2 ray_vs_sphere( float3 p, float3 dir, float r )
{
	float b = dot( p, dir );
	float c = dot( p, p ) - r * r;
	
	float d = b * b - c;
	if ( d < 0.0 ) {
		return float2(10000.0, -10000.0);
	}
	d = sqrt( d );
	
	return float2( -b - d, -b + d );
}

float optic( float3 p, float3 q, DensityProfile density_profile)
{
	float3 s = ( q - p ) / float( NUM_OUT_SCATTER );
	float3 v = p + s * 0.5;
	
	float sum = 0.0;
	for ( int i = 0; i < NUM_OUT_SCATTER; i++ ) 
	{
		sum += GetProfileDensity(density_profile, length(v) - mAtmosphere.mBottomRadius);
		v += s;
	}
	sum *= length( s );
	
	return sum;
}

float3 in_scatter( float3 o, float3 dir, float2 e, float3 l )
{	
	float3 sum_ray = 0.0;
	float3 sum_mie = 0.0;
	
	float n_ray0 = 0.0;
	float n_mie0 = 0.0;
	float n_ozone0 = 0.0;
	
	float len = ( e.y - e.x ) / float( NUM_IN_SCATTER );
	float3 s = dir * len;
	float3 v = o + dir * ( e.x + len * 0.5 );
	
	// [NOTE]
	// Basically same as IntegrateSingleScattering
	// Density is accumulated here instead of transmittance

	// [TODO] Aerial Perspective and Shadow 
	for ( int i = 0; i < NUM_IN_SCATTER; i++, v += s )
	{
		float d_ray = GetProfileDensity(mAtmosphere.mRayleighDensity, length(v) - mAtmosphere.mBottomRadius) * len;
		float d_mie = GetProfileDensity(mAtmosphere.mMieDensity, length(v) - mAtmosphere.mBottomRadius) * len;
		float d_ozone = GetProfileDensity(mAtmosphere.mOzoneDensity, length(v) - mAtmosphere.mBottomRadius) * len;
		
		n_ray0 += d_ray;
		n_mie0 += d_mie;
		n_ozone0 += d_ozone;
		
		float2 f = ray_vs_sphere( v, l, mAtmosphere.mTopRadius );
		float3 u = v + l * f.y;

		float n_ray1 = optic( v, u, mAtmosphere.mRayleighDensity );
		float n_mie1 = optic( v, u, mAtmosphere.mMieDensity );
		float n_ozone1 = optic( v, u, mAtmosphere.mOzoneDensity );
		
		// Transmittance: Eye to v then v to Sun
		float3 att = exp( 
			- ( n_ray0 + n_ray1 ) * mAtmosphere.mRayleighExtinction 
			- ( n_mie0 + n_mie1 ) * mAtmosphere.mMieExtinction
			- ( n_ozone0 + n_ozone1 ) * mAtmosphere.mOzoneExtinction
		);

		sum_ray += d_ray * att;
		sum_mie += d_mie * att;
	}

	float c  = dot( dir, l );
	float3 scatter =
		sum_ray * mAtmosphere.mSolarIrradiance * mAtmosphere.mRayleighScattering * RayleighPhaseFunction( c ) +
	 	sum_mie * mAtmosphere.mSolarIrradiance * mAtmosphere.mMieScattering * MiePhaseFunction( mAtmosphere.mMiePhaseFunctionG, c );

	// [TODO]
	float3 white_point = float3(1, 1, 1);
	float exposure = 10.0;
	return 1 - exp(-scatter / white_point * exposure);

	// return 10.0 * scatter;
}

float3 RaymarchAtmosphereScattering(float3 inPosition, float3 inDirection)
{
	float3 output = 0;

	// Position in km
	float3 eye = inPosition + float3(0, mAtmosphere.mBottomRadius, 0);
	float3 dir = inDirection;

	float2 atmosphere_hit = ray_vs_sphere( eye, dir, mAtmosphere.mTopRadius );
	float2 earth_hit = ray_vs_sphere( eye, dir, mAtmosphere.mBottomRadius );

	if ( atmosphere_hit.x > atmosphere_hit.y )
		return 0; // no hit on atmosphere

	bool visualize = 0;
	float2 from_to = 0;
	if (atmosphere_hit.x < 0)
	{
		// inside atmophere
		if (earth_hit.x < 0 && earth_hit.y > 0)
		{
			// inside earth
			from_to = float2(earth_hit.y, atmosphere_hit.y);

			if (visualize)
				return float3(1,0,0);
		}
		else
		{
			// outside earth
			if (earth_hit.y > 0)
			{
				// hit earth
				from_to = float2(0, earth_hit.x);

				if (visualize)
					return float3(1,1,0);
			}
			else
			{
				// hit atmosphere
				from_to = float2(0, atmosphere_hit.y);

				if (visualize)
					return float3(0,0,1);
			}
		}
	}
	else
	{
		// outside atmosphere
		from_to = float2(atmosphere_hit.x, min(earth_hit.x, atmosphere_hit.y));

		if (visualize)
			return float3(0,1,0);
	}

	return in_scatter( eye, dir, from_to, mPerFrame.mSunDirection.xyz );
}