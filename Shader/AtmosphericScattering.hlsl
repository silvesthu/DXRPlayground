// From https://www.shadertoy.com/view/lslXDr

// ray intersects sphere
// e = -b +/- sqrt( b^2 - c )
float2 ray_vs_sphere( float3 p, float3 dir, float r ) {
	float b = dot( p, dir );
	float c = dot( p, p ) - r * r;
	
	float d = b * b - c;
	if ( d < 0.0 ) {
		return float2(10000.0, -10000.0);
	}
	d = sqrt( d );
	
	return float2( -b - d, -b + d );
}

// scatter const
// Match earth/atmosphere radius with Bruneton 2017
#define EARTH_RADIUS 		6.360		// [1e6m]
#define ATMOSPHERE_RADIUS 	6.420		// [1e6m]

#define NUM_OUT_SCATTER 8
#define NUM_IN_SCATTER 80

// Mie
// g : ( -0.75, -0.999 )
//      3 * ( 1 - g^2 )               1 + c^2
// F = ----------------- * -------------------------------
//      8pi * ( 2 + g^2 )     ( 1 + g^2 - 2 * g * c )^(3/2)
float phase_mie( float g, float c, float cc ) {
	float gg = g * g;
	
	float a = ( 1.0 - gg ) * ( 1.0 + cc );

	float b = 1.0 + gg - 2.0 * g * c;
	b *= sqrt( b );
	b *= 2.0 + gg;	
	
	return ( 3.0 / 8.0 / MATH_PI ) * a / b;
}

// Rayleigh
// g : 0
// F = 3/16PI * ( 1 + c^2 )
float phase_ray( float cc ) {
	return ( 3.0 / 16.0 / MATH_PI) * ( 1.0 + cc );
}

float density( float3 p, float ph ) {
	return exp( -max( length( p ) - EARTH_RADIUS, 0.0 ) / ph );
}

float optic( float3 p, float3 q, float ph ) {
	float3 s = ( q - p ) / float( NUM_OUT_SCATTER );
	float3 v = p + s * 0.5;
	
	float sum = 0.0;
	for ( int i = 0; i < NUM_OUT_SCATTER; i++ ) {
		sum += density( v, ph );
		v += s;
	}
	sum *= length( s );
	
	return sum;
}

float3 in_scatter( float3 o, float3 dir, float2 e, float3 l ) {
	// Match rayleigh/mie scaled depth with Bruneton 2017
	const float scaled_height_rayleigh 						= 0.008;							// [1e6m]
    const float scaled_height_mie 							= 0.0012;							// [1e6m]    
    const float3 scattering_coefficient_rayleigh 			= float3(5.8, 13.5, 33.1);			// [1e6m]
    const float3 scattering_coefficient_mie 				= 21.0;								// [1e6m] 
    const float extinction_coefficient_mie_multiplier 		= 1.11;								// [1e6m] from scattering_coefficient_mie / extinction_coefficient_mie = 0.9 in paper
    
	float3 sum_ray = 0.0;
    float3 sum_mie = 0.0;
    
    float n_ray0 = 0.0;
    float n_mie0 = 0.0;
    
	float len = ( e.y - e.x ) / float( NUM_IN_SCATTER );
    float3 s = dir * len;
	float3 v = o + dir * ( e.x + len * 0.5 );
    
    for ( int i = 0; i < NUM_IN_SCATTER; i++, v += s ) {   
		float d_ray = density( v, scaled_height_rayleigh ) * len;
        float d_mie = density( v, scaled_height_mie ) * len;
        
        n_ray0 += d_ray;
        n_mie0 += d_mie;
        
        float2 f = ray_vs_sphere( v, l, ATMOSPHERE_RADIUS );
		float3 u = v + l * f.y;
        
        float n_ray1 = optic( v, u, scaled_height_rayleigh );
        float n_mie1 = optic( v, u, scaled_height_mie );
		
        float3 att = exp( - ( n_ray0 + n_ray1 ) * scattering_coefficient_rayleigh - ( n_mie0 + n_mie1 ) * scattering_coefficient_mie * extinction_coefficient_mie_multiplier );
        
		sum_ray += d_ray * att;
        sum_mie += d_mie * att;
	}
	
	float c  = dot( dir, -l );
	float cc = c * c;
    float3 scatter =
        sum_ray * scattering_coefficient_rayleigh * phase_ray( cc ) +
     	sum_mie * scattering_coefficient_mie * phase_mie( -0.76, c, cc );

	return 10.0 * scatter;
}

float3 AtmosphereScattering(float3 inPosition, float3 inDirection)
{
	float3 output = 0;

	float3 base_altitude = float3(0, 1000, 0);
	float3 eye = (base_altitude + inPosition) * 1e-6 + float3(0, EARTH_RADIUS, 0);
	float3 dir = inDirection;

	float2 atmosphere_hit = ray_vs_sphere( eye, dir, ATMOSPHERE_RADIUS );
	float2 earth_hit = ray_vs_sphere( eye, dir, EARTH_RADIUS );

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