#include "Shared.h"

CloudMode GetCloudMode()
{
#ifdef kCloudMode
	// Static
	return kCloudMode;
#else
	// Dynamic
	return mConstants.mCloud.mMode;
#endif // kloudMode
}

// [Schneider16]
float SampleCloudDensity(float3 p, bool sample_coarse)
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

	float density = shape;

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

	switch (GetCloudMode())
	{
	case CloudMode::None: return;
	case CloudMode::Noise: break;
	default: return;
	}

	float3 accumulated_light = 0;
	float accumulated_density = 0.0;

	// Stubs
	int zero_density_max = 6;

	// Range
	float2 range = 0;

	// Ignore alto for now
	float2 distance_to_planet = 0;
	bool hit_planet = IntersectRaySphere(PlanetRayOrigin(), PlanetRayDirection(), PlanetCenter(), PlanetRadius() + 0, distance_to_planet);
	float2 distance_to_strato = 0;
	bool hit_strato = IntersectRaySphere(PlanetRayOrigin(), PlanetRayDirection(), PlanetCenter(), PlanetRadius() + mConstants.mCloud.mGeometry.mStrato, distance_to_strato);
	float2 distance_to_cirro = 0;
	bool hit_cirro = IntersectRaySphere(PlanetRayOrigin(), PlanetRayDirection(), PlanetCenter(), PlanetRadius() + mConstants.mCloud.mGeometry.mCirro, distance_to_cirro);

	if (PlanetRayOrigin().y < mConstants.mCloud.mGeometry.mStrato) // camera below strato
	{
		if (distance_to_planet.x > 0) // hit ground
			return;

		range = float2(distance_to_strato.y, distance_to_cirro.y);
	}
	else if (PlanetRayOrigin().y > mConstants.mCloud.mGeometry.mCirro) // camera above cirro
	{
		if (!hit_cirro || distance_to_cirro.x < 0) // no hit
			return;

		range = float2(distance_to_cirro.x, distance_to_cirro.y);

		if (hit_strato)
			range.y = distance_to_strato.x;
	}
	else // camera inside cloud
	{
		// [TODO] Need to adjust sampling point to make entrance and exit smooth

		range = 0;

		if (distance_to_strato.x > 0)
			range.y = distance_to_strato.x;
		else
			range.y = distance_to_cirro.y;
	}

	if (range.x >= range.y)
		return;

	if (range.x > 1000)
		return;

	int sample_count = mConstants.mCloud.mRaymarch.mSampleCount;
	float step_length = (range.y - range.x) / sample_count;
	float3 step = PlanetRayDirection() * step_length;

	// Move start position
	float3 position = PlanetRayOrigin();
	position += PlanetRayDirection() * (range.x + step_length * 0.5);
	for (int i = 0; i < min(sample_count, (range.y - range.x) / step_length) ; i++)
	{
		float density = SampleCloudDensity(position, false);

		// Lighting
		{
			float3 ray_end;
			float density = SampleCloudDensityAlongCone(position, GetSunDirection(), ray_end);

			float light_samples = density * 1.0;

			float3 sky_irradiance = mConstants.mAtmosphere.mSolarIrradiance * 0.1;
			float3 sun_irradiance = mConstants.mAtmosphere.mSolarIrradiance;

			// [Schneider16]
			float powder_sugar_effect = 1.0 - exp(-light_samples * 2.0);
			float beers_law = exp(-light_samples);
			float light_energy = 2.0 * beers_law * powder_sugar_effect;

			float phase = PhaseFunction_HenyeyGreenstein(0.2, dot(PlanetRayDirection(), GetSunDirection()));
			light_energy *= phase;

			accumulated_light += (1 - accumulated_density) * light_energy * (sun_irradiance + sky_irradiance);
		}

		// Density
		{
			accumulated_density += density;
			if (accumulated_density >= 1.0)
			{
				accumulated_density = 1.0;
				break;
			}
		}

		position += step;
	}

	outTransmittance = 1.0 - accumulated_density;
	outLuminance = accumulated_light;
}
