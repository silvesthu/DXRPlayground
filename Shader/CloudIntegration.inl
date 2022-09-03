
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// https://www.shadertoy.com/view/ll3SWl

// hash function
float hash(float n)
{
	return frac(cos(n) * 114514.1919);
}

// 3d noise function
float noise(float3 x)
{
	float3 p = floor(x);
	float3 f = smoothstep(0.0, 1.0, frac(x));

	float n = p.x + p.y * 10.0 + p.z * 100.0;

	return lerp(
		lerp(lerp(hash(n + 0.0), hash(n + 1.0), f.x),
			lerp(hash(n + 10.0), hash(n + 11.0), f.x), f.y),
		lerp(lerp(hash(n + 100.0), hash(n + 101.0), f.x),
			lerp(hash(n + 110.0), hash(n + 111.0), f.x), f.y), f.z);
}

// Fractional Brownian motion
float fbm(float3 p)
{
	float3x3 m = float3x3(0.00, 1.60, 1.20, -1.60, 0.72, -0.96, -1.20, -0.96, 1.28);

	float f = 0.5000 * noise(p);
	p = mul(m, p);
	f += 0.2500 * noise(p);
	p = mul(m, p);
	f += 0.1666 * noise(p);
	p = mul(m, p);
	f += 0.0834 * noise(p);
	return f;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// [Schneider16]
float SampleCloudDensity(float3 p, bool sample_coarse)
{
	float density = 0;

	// FBM noise
	if (false)
	{
		float frequency = mConstants.mCloud.mShapeNoise.mFrequency * 10.0;
		float power = mConstants.mCloud.mShapeNoise.mPower * 0.2;
		float scale = mConstants.mCloud.mShapeNoise.mScale;
		float3 offset = mConstants.mCloud.mShapeNoise.mOffset;

		float shape = fbm((p + offset) * frequency);
		shape = pow(shape, power) * scale;

		density = shape;
	}

	// Noise texture
	// if (false)
	{
		float frequency = mConstants.mCloud.mShapeNoise.mFrequency;
		float power = mConstants.mCloud.mShapeNoise.mPower;
		float scale = mConstants.mCloud.mShapeNoise.mScale;
		float3 offset = mConstants.mCloud.mShapeNoise.mOffset;

		// [TODO] Skew

		float shape = CloudShapeNoiseSRV.SampleLevel(BilinearWrapSampler, (p + offset) * frequency, 0).x;
		shape = pow(shape, power) * scale;

		float erosion = CloudErosionNoiseSRV.SampleLevel(BilinearWrapSampler, (p + offset * 0.9) * frequency * 2, 0).x;
		// shape *= saturate(pow(erosion, 4));

		density = shape;
	}

	// Debug
	// density = 0.02;

	return density;
}

float SampleCloudDensityAlongCone(float3 p, float3 ray_direction, out float3 ray_end)
{
	float accumulated_density = 0.0;

	float3 light_step = ray_direction * mConstants.mCloud.mRaymarch.mLightSampleLength;

	p += light_step * 0.5;
	for (int i = 0; i < mConstants.mCloud.mRaymarch.mLightSampleCount; i++)
	{
		// [TODO] line -> cone
		p += light_step;

		// [TODO] utilize coarse
		accumulated_density += SampleCloudDensity(p, false);
	}
	p += light_step * 0.5;
	ray_end = p;

	return accumulated_density;
}

void RaymarchCloud(out float3 outTransmittance, out float3 outLuminance)
{
	outTransmittance = 1.0;
	outLuminance = 0.0;

	if (mConstants.mCloud.mMode == CloudMode::None)
		return;

	float3 accumulated_light = 0;
	float accumulated_density = 0.0;

	// Cloud
	{
		// Stubs
		int zero_density_max = 6;

		// Bottom of cloud scape
		float2 distance_range = 0;

		// Ignore alto for now
		float2 distance_to_planet = 0;
		bool hit_planet = IntersectRaySphere(PlanetRayOrigin(), PlanetRayDirection(), PlanetCenter(), PlanetRadius() + 0, distance_to_planet);
		float2 distance_to_strato = 0;
		bool hit_strato = IntersectRaySphere(PlanetRayOrigin(), PlanetRayDirection(), PlanetCenter(), PlanetRadius() + mConstants.mCloud.mGeometry.mStrato, distance_to_strato);
		float2 distance_to_cirro = 0;
		bool hit_cirro = IntersectRaySphere(PlanetRayOrigin(), PlanetRayDirection(), PlanetCenter(), PlanetRadius() + mConstants.mCloud.mGeometry.mCirro, distance_to_cirro);

		if (!hit_cirro || distance_to_cirro.y < 0)
			return; // Hit nothing

		if (!hit_strato || distance_to_strato.y < 0)
			distance_range = float2(0, distance_to_cirro.y); // Hit cirro only, inside cloud scape
		else
		{
			if (distance_to_strato.x > 0)
				distance_range = float2(0, distance_to_strato.x); // Hit strato, inside cloud scape
			else
				if (hit_planet && distance_to_planet.x > 0 && distance_to_planet.x < distance_to_strato.y)
					return; // Hit ground
				else
					distance_range = float2(distance_to_strato.y, distance_to_cirro.y); // Hit strato, below cloud scape
		}

		int sample_count = mConstants.mCloud.mRaymarch.mSampleCount;
		float step_length = (distance_range.y - distance_range.x) / sample_count;
		float3 step = PlanetRayDirection() * step_length;

		// [Debug]
		if (false)
		{
			outTransmittance = 0;
			outLuminance = distance_range.y - distance_range.x;
			return;
		}

		int zero_density_count = 0;
		bool sample_coarse = true;

		// Move start position
		float3 position = PlanetRayOrigin();
		position += PlanetRayDirection() * (distance_range.x + step_length * 0.5);
		for (int i = 0; i < sample_count; i++)
		{
			// [Debug]
			sample_coarse = false; // Not supported yet

			float density = SampleCloudDensity(position, sample_coarse);
			if (sample_coarse)
			{
				if (density != 0.0)
				{
					i--; // step back
					sample_coarse = false;
					continue;
				}

				position += step;
			}
			else
			{
				if (density == 0.0)
					zero_density_count++;

				if (zero_density_count == zero_density_max)
				{
					zero_density_count = 0;
					sample_coarse = true;
					continue;
				}

				// Lighting
				{
					float3 ray_end;
					float density = SampleCloudDensityAlongCone(position, GetSunDirection(), ray_end);

					float light_samples = density * 1.0;

					float3 sky_irradiance = 0;
					float3 sun_irradiance = 0;
					GetSunAndSkyIrradiance(ray_end, normalize(ray_end - PlanetCenter()), sun_irradiance, sky_irradiance);

					// [Schneider16]
					float powder_sugar_effect = 1.0 - exp(-light_samples * 2.0);
					float beers_law = exp(-light_samples);
					float light_energy = 2.0 * beers_law * powder_sugar_effect;

					float phase = PhaseFunction_HenyeyGreenstein(0.2, dot(PlanetRayDirection(), GetSunDirection()));
					light_energy *= phase;

					accumulated_light += (1 - accumulated_density) * light_energy * (sun_irradiance + sky_irradiance);
				}

				accumulated_density += density;
				position += step;
			}

			if (accumulated_density >= 1.0)
			{
				accumulated_density = 1.0;
				break;
			}
		}

		// [Debug]
		// accumulated_light = SampleCloudDensity(PlanetRayOrigin() + PlanetRayDirection() * 10, true);
		// accumulated_light = accumulated_density;
	}

	outTransmittance = 1.0 - accumulated_density;
	outLuminance = accumulated_light;
}
