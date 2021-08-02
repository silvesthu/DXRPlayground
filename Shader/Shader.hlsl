#include "Common.hlsl"

#define CONSTANT_DEFAULT(x)
#include "Generated/Enum.hlsl"
#include "ShaderType.hlsl"

#ifndef SHADER_PROFILE_LIB
#define ENABLE_INLINE_RAYTRACING
#endif // SHADER_PROFILE_LIB

const static float kPreExposure = 1.0e-4;
const static float kEmissionScale = 1.0e4;

//////////////////////////////////////////////////////////////////////////////////
// Common

RWTexture2D<float4> RaytracingOutput : register(u0, space0);
cbuffer PerFrameBuffer : register(b0, space0)
{
	PerFrame mPerFrame;
};

SamplerState BilinearSampler : register(s0);
SamplerState BilinearWrapSampler : register(s1);

//////////////////////////////////////////////////////////////////////////////////
// Atmosphere & Cloud

float3 RadianceToLuminance(float3 radiance)
{
	// https://en.wikipedia.org/wiki/Luminous_efficacy
	// https://en.wikipedia.org/wiki/Sunlight#Measurement

	float kW_to_W = 1000.0;
	float3 luminance = radiance * kW_to_W * kSunLuminousEfficacy * kPreExposure;
	return luminance;

	// [Bruneton17]
	if (0)
	{
		float3 white_point = float3(1, 1, 1);
		float exposure = 10.0;
		return 1 - exp(-radiance / white_point * exposure);
	}
}

#include "Atmosphere.hlsl"
cbuffer CloudBuffer : register(b0, space3)
{
	Cloud mCloud;
}
Texture3D<float4> CloudShapeNoiseSRV : register(t0, space3);
Texture3D<float4> CloudErosionNoiseSRV : register(t1, space3);

//////////////////////////////////////////////////////////////////////////////////
// DXR

// Proxies
static uint3 sDispatchRaysIndex;
uint3 sGetDispatchRaysIndex() 
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sDispatchRaysIndex;
#else
	return DispatchRaysIndex();
#endif // ENABLE_INLINE_RAYTRACING
}

static uint3 sDispatchRaysDimensions;
uint3 sGetDispatchRaysDimensions() 
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sDispatchRaysDimensions;
#else
	return DispatchRaysDimensions();
#endif // ENABLE_INLINE_RAYTRACING
}

static float3 sWorldRayOrigin;
float3 sGetWorldRayOrigin()
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sWorldRayOrigin;
#else
	return WorldRayOrigin();
#endif // ENABLE_INLINE_RAYTRACING
}

static float3 sWorldRayDirection;
float3 sGetWorldRayDirection()
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sWorldRayDirection;
#else
	return WorldRayDirection();
#endif // ENABLE_INLINE_RAYTRACING
}

static float sRayTCurrent;
float sGetRayTCurrent()
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sRayTCurrent;
#else
	return RayTCurrent();
#endif // ENABLE_INLINE_RAYTRACING
}

static uint sInstanceIndex;
uint sGetInstanceIndex()
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sInstanceIndex;
#else
	return InstanceIndex();
#endif // ENABLE_INLINE_RAYTRACING
}

static uint sPrimitiveIndex;
uint sGetPrimitiveIndex()
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sPrimitiveIndex;
#else
	return PrimitiveIndex();
#endif // ENABLE_INLINE_RAYTRACING
}

static uint sGeometryIndex;
uint sGetGeometryIndex()
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sGeometryIndex;
#else
	return GeometryIndex();
#endif // ENABLE_INLINE_RAYTRACING
}

static uint sInstanceID;
uint sGetInstanceID()
{
#ifdef ENABLE_INLINE_RAYTRACING
	return sInstanceID;
#else
	return InstanceID();
#endif // ENABLE_INLINE_RAYTRACING
}

RaytracingAccelerationStructure RaytracingScene : register(t0, space0);
StructuredBuffer<InstanceData> InstanceDataBuffer : register(t1, space0);
StructuredBuffer<uint> Indices : register(t2, space0);
StructuredBuffer<float3> Vertices : register(t3, space0);
StructuredBuffer<float3> Normals : register(t4, space0);

#define DEBUG_PIXEL_RADIUS (3)

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 Tonemap_knarkowicz(float3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 LuminanceToColor(float3 luminance)
{
	// Exposure
	float3 normalized_luminance = 0;
	{
		// https://google.github.io/filament/Filament.htmdl#physicallybasedcamera

		float kSaturationBasedSpeedConstant = 78.0f;
		float kISO = 100;
		float kVignettingAttenuation = 0.78f; // To cancel out saturation. Typically 0.65 for real lens, see https://www.unrealengine.com/en-US/tech-blog/how-epic-games-is-handling-auto-exposure-in-4-25
		float kLensSaturation = kSaturationBasedSpeedConstant / kISO / kVignettingAttenuation;

		float exposure_normalization_factor = 1.0 / (pow(2.0, mPerFrame.mEV100) * kLensSaturation); // = 1.0 / luminance_max
		normalized_luminance = luminance * (exposure_normalization_factor / kPreExposure);

		// [Reference]
		// https://en.wikipedia.org/wiki/Exposure_value
		// https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
		// https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
		// https://docs.unrealengine.com/en-US/RenderingAndGraphics/PostProcessEffects/ColorGrading/index.html
	}

	float3 tonemapped_color = 0;
	// Tonemap
	{
		switch (mPerFrame.mTonemapMode)
		{
		default:
		case TonemapMode_Passthrough: tonemapped_color = normalized_luminance; break;
		case TonemapMode_knarkowicz: tonemapped_color = Tonemap_knarkowicz(normalized_luminance); break;
		}

		// [Reference]
		// https://github.com/ampas/aces-dev
		// https://docs.unrealengine.com/en-US/RenderingAndGraphics/PostProcessEffects/ColorGrading/index.html
	}

	return tonemapped_color;
}

// Adapters
float3 RayOrigin() { return sGetWorldRayOrigin() * mAtmosphere.mSceneScale; }
float3 RayDirection() { return sGetWorldRayDirection(); }
float3 RayHitPosition() { return (sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent()) * mAtmosphere.mSceneScale; }
float3 PlanetCenter() { return float3(0, -mAtmosphere.mBottomRadius, 0); }
float PlanetRadius() { return mAtmosphere.mBottomRadius; }
float3 GetSunDirection() { return mPerFrame.mSunDirection.xyz; }

float3 GetExtrapolatedSingleMieScattering(float4 scattering)
{
	// Algebraically this can never be negative, but rounding errors can produce
	// that effect for sufficiently short view rays.
	if (scattering.r <= 0.0)
		return 0.0;

	return scattering.rgb * scattering.a / scattering.r * (mAtmosphere.mRayleighScattering.r / mAtmosphere.mMieScattering.r) * (mAtmosphere.mMieScattering / mAtmosphere.mRayleighScattering);
}

void GetCombinedScattering(float r, float mu, float mu_s, float nu, bool ray_r_mu_intersects_ground, out float3 rayleigh_scattering, out float3 single_mie_scattering)
{
	float4 scattering = GetScattering(ScatteringSRV, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
	float3 solar_irradiance = mAtmosphere.mPrecomputeWithSolarIrradiance ? 1.0 : mAtmosphere.mSolarIrradiance;

	single_mie_scattering = GetExtrapolatedSingleMieScattering(scattering) * solar_irradiance;
	rayleigh_scattering = scattering.xyz * solar_irradiance;
}

void GetSkyRadiance(out float3 sky_radiance, out float3 transmittance_to_top)
{
	sky_radiance = 0;
	transmittance_to_top = 1;

	float3 camera = RayOrigin() - PlanetCenter();
	float3 view_ray = RayDirection();
	float3 sun_direction = GetSunDirection();

	float r = length(camera);
	float rmu = dot(camera, view_ray);
	float distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + mAtmosphere.mTopRadius * mAtmosphere.mTopRadius);

	if (distance_to_top_atmosphere_boundary > 0.0) 
	{
		// Outer space

		// Move camera to top of atmosphere along view direction
		camera = camera + view_ray * distance_to_top_atmosphere_boundary;
		r = mAtmosphere.mTopRadius;
		rmu += distance_to_top_atmosphere_boundary;
	}
	else if (r > mAtmosphere.mTopRadius) 
	{
		// No hit
		return;
	}

	float mu = rmu / r;
	float mu_s = dot(camera, sun_direction) / r;
	float nu = dot(view_ray, sun_direction);
	bool ray_r_mu_intersects_ground = RayIntersectsGround(r, mu);

	// override
	{
		//r = 6360;
		//mu = 0.0;
		//mu_s = 0.0;
		//nu = 0.0;
		//ray_r_mu_intersects_ground = false;
	}

	transmittance_to_top = ray_r_mu_intersects_ground ? 0 : GetTransmittanceToTopAtmosphereBoundary(r, mu);

	// [TODO] shadow
	float3 rayleigh_scattering;
	float3 single_mie_scattering;
	GetCombinedScattering(r, mu, mu_s, nu, ray_r_mu_intersects_ground, rayleigh_scattering, single_mie_scattering);

	// [TODO] light shafts

	sky_radiance = rayleigh_scattering * RayleighPhaseFunction(nu) + single_mie_scattering * MiePhaseFunction(mAtmosphere.mMiePhaseFunctionG, nu);

	// override
	{
		//sky_radiance = float3(1.1, 1.2, 1.3);
		//float3 uvw0, uvw1;
		//float s;
		//Encode4D(float4(r, mu, mu_s, nu), ray_r_mu_intersects_ground, ScatteringSRV, uvw0, uvw1, s);
		//rayleigh_scattering = uvw0;
		//rayleigh_scattering = ScatteringSRV.SampleLevel(BilinearSampler, uvw0, 0);
	}
}

// Aerial Perspective
void GetSkyRadianceToPoint(out float3 sky_radiance, out float3 transmittance)
{
	// [TODO] Occlusion?

	sky_radiance = 0;
	transmittance = 1;

	if (mAtmosphere.mAerialPerspective == 0)
		return;

	float3 hit_position = RayHitPosition() - PlanetCenter();
	float3 camera = RayOrigin() - PlanetCenter();
	float3 sun_direction = GetSunDirection();

	float3 view_ray = RayDirection();
	float r = length(camera);
	float rmu = dot(camera, view_ray);
	float distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + mAtmosphere.mTopRadius * mAtmosphere.mTopRadius);

	// If the viewer is in space and the view ray intersects the atmosphere, move
	// the viewer to the top atmosphere boundary (along the view ray):
	if (distance_to_top_atmosphere_boundary > 0.0)
	{
		// Outer space

		// Move camera to top of atmosphere along view direction
		camera = camera + view_ray * distance_to_top_atmosphere_boundary;
		r = mAtmosphere.mTopRadius;
		rmu += distance_to_top_atmosphere_boundary;
	}
	else if (r > mAtmosphere.mTopRadius)
	{
		// No hit
		return;
	}

	// Compute the r, mu, mu_s and nu parameters for the first texture lookup.
	float mu = rmu / r;
	float mu_s = dot(camera, sun_direction) / r;
	float nu = dot(view_ray, sun_direction);
	float d = length(hit_position - camera);
	bool ray_r_mu_intersects_ground = RayIntersectsGround(r, mu);

	transmittance = GetTransmittance(r, mu, d, ray_r_mu_intersects_ground);

	float3 rayleigh_scattering;
	float3 single_mie_scattering;
	GetCombinedScattering(r, mu, mu_s, nu, ray_r_mu_intersects_ground, rayleigh_scattering, single_mie_scattering);

	// [TODO] shadow
	float shadow_length = 0;

	// Compute the r, mu, mu_s and nu parameters for the second texture lookup.
	// If shadow_length is not 0 (case of light shafts), we want to ignore the
	// rayleigh_scattering along the last shadow_length meters of the view ray, which we
	// do by subtracting shadow_length from d (this way rayleigh_scattering_p is equal to
	// the S|x_s=x_0-lv term in Eq. (17) of our paper).
	d = max(d - shadow_length, 0.0);
	float r_p = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));
	float mu_p = (r * mu + d) / r_p;
	float mu_s_p = (r * mu_s + d * nu) / r_p;
	float3 rayleigh_scattering_p = 0;
	float3 single_mie_scattering_p = 0;

	// [TODO] artifact near horizon
	GetCombinedScattering(r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground, rayleigh_scattering_p, single_mie_scattering_p);

	// Combine the lookup results to get the scattering between camera and point.
	float3 shadow_transmittance = transmittance;
	if (shadow_length > 0.0)
	{
		// This is the T(x,x_s) term in Eq. (17) of our paper, for light shafts.
		shadow_transmittance = GetTransmittance(r, mu, d, ray_r_mu_intersects_ground);
	}

	rayleigh_scattering = rayleigh_scattering - shadow_transmittance * rayleigh_scattering_p;
	single_mie_scattering = single_mie_scattering - shadow_transmittance * single_mie_scattering_p;

	single_mie_scattering = GetExtrapolatedSingleMieScattering(float4(rayleigh_scattering.rgb, single_mie_scattering.r));

	// Hack to avoid rendering artifacts when the sun is below the horizon.
	single_mie_scattering = single_mie_scattering * smoothstep(float(0.0), float(0.01), mu_s);

	sky_radiance = rayleigh_scattering* RayleighPhaseFunction(nu) + single_mie_scattering * MiePhaseFunction(mAtmosphere.mMiePhaseFunctionG, nu);
}

void GetSunAndSkyIrradiance(float3 hit_position, float3 normal, out float3 sun_irradiance, out float3 sky_irradiance)
{
	float3 local_position = hit_position - PlanetCenter();
	float3 sun_direction = GetSunDirection();

	float r = length(local_position);
	float mu_s = dot(local_position, sun_direction) / r;

	// Indirect irradiance (approximated if the surface is not horizontal).
	float3 solar_irradiance = mAtmosphere.mPrecomputeWithSolarIrradiance ? 1.0 : mAtmosphere.mSolarIrradiance;
	sky_irradiance = solar_irradiance * GetIrradiance(r, mu_s) * (1.0 + dot(normal, local_position) / r) * 0.5;

	// Direct irradiance.
	sun_irradiance = mAtmosphere.mSolarIrradiance * GetTransmittanceToSun(r, mu_s) * max(dot(normal, sun_direction), 0.0);
}

float GetSunVisibility(float3 position)
{
	float3 sun_direction = GetSunDirection();

	// [TODO]
	return 1.0;
}

float3 GetSkyVisibility(float3 position)
{
	// [TODO]
	return 1.0;
}

float3 GetEnvironmentEmission()
{
	// Sky
	float3 radiance = 0;
	float3 transmittance_to_top = 0;
	GetSkyRadiance(radiance, transmittance_to_top);

	// Ground (the planet)
	float2 distance = 0;
	if (IntersectRaySphere(RayOrigin(), RayDirection(), PlanetCenter(), PlanetRadius(), distance) && distance.x > 0)
	{
		float3 hit_position = RayOrigin() + RayDirection() * distance.x;
		float3 normal = normalize(hit_position - PlanetCenter());

		float3 kGroundAlbedo = mAtmosphere.mRuntimeGroundAlbedo;

		// Sky/Sun Irradiance -> Reflection -> Radiance
		float3 sky_irradiance = 0;
		float3 sun_irradiance = 0;
		GetSunAndSkyIrradiance(hit_position, normal, sun_irradiance, sky_irradiance);
		float3 ground_radiance = kGroundAlbedo * (1.0 / MATH_PI) * (sun_irradiance * GetSunVisibility(hit_position) + sky_irradiance * GetSkyVisibility(hit_position));

		// [TODO] lightshaft

		// Transmittance (merge with GetSkyRadiance()?)
		float r = length(RayOrigin() - PlanetCenter());
		float rmu = dot(RayOrigin() - PlanetCenter(), RayDirection());
		float mu = rmu / r;
		float3 transmittance_to_ground = GetTransmittance(r, mu, distance.x, true);

		// Debug - Global
		switch (mPerFrame.mDebugMode)
		{
		case DebugMode_Barycentrics: 			return 0;
		case DebugMode_Vertex: 					return hit_position;
		case DebugMode_Normal: 					return normal;
		case DebugMode_Albedo: 					return kGroundAlbedo;
		case DebugMode_Reflectance: 			return 0;
		case DebugMode_Emission: 				return 0;
		case DebugMode_Roughness: 				return 1;
		case DebugMode_Transmittance:			return transmittance_to_ground;
		case DebugMode_InScattering:			return radiance;
		default:								break;
		}

		// Blend
		if (mAtmosphere.mAerialPerspective == 0)
			radiance = ground_radiance;
		else
			radiance = radiance + transmittance_to_ground * ground_radiance;
	}
	else
	{
		// Debug - Global
		switch (mPerFrame.mDebugMode)
		{
		case DebugMode_Barycentrics: 			return 0;
		case DebugMode_Vertex: 					return RayDirection(); // rays are supposed to go infinity
		case DebugMode_Normal: 					return -RayDirection();
		case DebugMode_Albedo: 					return 0;
		case DebugMode_Reflectance: 			return 0;
		case DebugMode_Emission: 				return 0;
		case DebugMode_Roughness: 				return 1;
		case DebugMode_Transmittance:			return transmittance_to_top;
		case DebugMode_InScattering:			return radiance;
		default:								break;
		}
	}

	// Sun
	if (dot(RayDirection(), GetSunDirection()) > cos(mAtmosphere.mSunAngularRadius))
	{
		radiance = radiance + transmittance_to_top * mAtmosphere.mSolarIrradiance;
	}

	// Debug
	{
		// return sqrt(radiance);
		// return radiance;
	}

	return RadianceToLuminance(radiance);
}

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
		float frequency = mCloud.mShapeNoise.mFrequency * 10.0;
		float power = mCloud.mShapeNoise.mPower * 0.2;
		float scale = mCloud.mShapeNoise.mScale;
		float3 offset = mCloud.mShapeNoise.mOffset;

		float shape = fbm((p + offset) * frequency);
		shape = pow(shape, power) * scale;

		density = shape;
	}

	// Noise texture
	// if (false)
	{
		float frequency = mCloud.mShapeNoise.mFrequency;
		float power = mCloud.mShapeNoise.mPower;
		float scale = mCloud.mShapeNoise.mScale;
		float3 offset = mCloud.mShapeNoise.mOffset;

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

	float3 light_step = ray_direction * mCloud.mRaymarch.mLightSampleLength;

	p += light_step * 0.5;
	for (int i = 0; i < mCloud.mRaymarch.mLightSampleCount; i++)
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

float3 RaymarchCloud(out float3 transmittance)
{
	transmittance = 1.0;

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
		bool hit_planet = IntersectRaySphere(RayOrigin(), RayDirection(), PlanetCenter(), PlanetRadius() + 0, distance_to_planet);
		float2 distance_to_strato = 0;
		bool hit_strato = IntersectRaySphere(RayOrigin(), RayDirection(), PlanetCenter(), PlanetRadius() + mCloud.mGeometry.mStrato, distance_to_strato);
		float2 distance_to_cirro = 0;
		bool hit_cirro = IntersectRaySphere(RayOrigin(), RayDirection(), PlanetCenter(), PlanetRadius() + mCloud.mGeometry.mCirro, distance_to_cirro);

		if (!hit_cirro || distance_to_cirro.y < 0)
			return 0; // Hit nothing

		if (!hit_strato || distance_to_strato.y < 0)
			distance_range = float2(0, distance_to_cirro.y); // Hit cirro only, inside cloud scape
		else
		{
			if (distance_to_strato.x > 0)
				distance_range = float2(0, distance_to_strato.x); // Hit strato, inside cloud scape
			else
				if (hit_planet && distance_to_planet.x > 0 && distance_to_planet.x < distance_to_strato.y)
					return 1; // Hit ground
				else
					distance_range = float2(distance_to_strato.y, distance_to_cirro.y); // Hit strato, below cloud scape
		}

		int sample_count = mCloud.mRaymarch.mSampleCount;
		float step_length = (distance_range.y - distance_range.x) / sample_count;
		float3 step = RayDirection() * step_length;

		// [Debug]
		if (false)
		{
			transmittance = 0;
			return distance_range.y - distance_range.x;
		}

		int zero_density_count = 0;
		bool sample_coarse = true;

		// Move start position
		float3 position = RayOrigin();
		position += RayDirection() * (distance_range.x + step_length * 0.5);
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

					float phase = PhaseFunction_HenyeyGreenstein(0.2, dot(RayDirection(), GetSunDirection()));
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
		// accumulated_light = SampleCloudDensity(RayOrigin() + RayDirection() * 10, true);
		// accumulated_light = accumulated_density;
	}

	transmittance = 1.0 - accumulated_density;
	return accumulated_light;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

[shader("miss")]
void DefaultMiss(inout RayPayload payload)
{
	payload.mDone = true;

	float3 atmosphere = 0;
	switch (mAtmosphere.mMode)
	{
	default:
	case AtmosphereMode_ConstantColor: 				atmosphere = mAtmosphere.mConstantColor.xyz; break;
	case AtmosphereMode_RaymarchAtmosphereOnly: 	atmosphere = RaymarchAtmosphereScattering(RayOrigin(), RayDirection()); break;
	case AtmosphereMode_PrecomputedAtmosphere: 		atmosphere = GetEnvironmentEmission(); break;
	case AtmosphereMode_Hillaire20: 				atmosphere = GetEnvironmentEmission(); break;
	}

	float3 cloud = 0;
	float3 cloud_transmittance = 1;
	switch (mCloud.mMode)
	{
	default:
	case CloudMode_None:							break;
	case CloudMode_Noise:							cloud = RaymarchCloud(cloud_transmittance); break;
	}

	// [TODO] How to mix contributions to get best result?
	float3 emission = 0;
	emission = atmosphere;
	emission = lerp(emission, cloud, 1.0 - cloud_transmittance);

	payload.mEmission = payload.mEmission + payload.mThroughput * emission;
}

HitInfo HitInternal(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	HitInfo hit_info = (HitInfo)0;
	hit_info.mPDF = 1.0;
	hit_info.mScatteringPDF = 1.0;
	hit_info.mTransmittance = 1.0;
	hit_info.mInScattering = 0.0;

	// See https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html for more system value intrinsics
	float3 barycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);

	// 16bit index is not supported. See https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl for reference
	uint index_count_per_triangle = 3;
	uint base_index = sGetPrimitiveIndex() * index_count_per_triangle + InstanceDataBuffer[sGetInstanceID()].mIndexOffset;
	uint3 indices = uint3(Indices[base_index], Indices[base_index + 1], Indices[base_index + 2]);

	// Attributes
	float3 normals[3] = { Normals[indices[0]], Normals[indices[1]], Normals[indices[2]] };
	float3 normal = normalize(normals[0] * barycentrics.x + normals[1] * barycentrics.y + normals[2] * barycentrics.z);

	float3 vertices[3] = { Vertices[indices[0]], Vertices[indices[1]], Vertices[indices[2]] };
	float3 vertex = vertices[0] * barycentrics.x + vertices[1] * barycentrics.y + vertices[2] * barycentrics.z;

	// Handle front face only
	if (dot(normal, -sGetWorldRayDirection()) < 0)
	{
		bool flip_normal = true;
		if (flip_normal)
			normal = -normal;
		else
		{
			hit_info.mDone = true;
			return hit_info;
		}
	}

	// Hit position
	float3 raw_hit_position = sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent();
	hit_info.mPosition = raw_hit_position + normal * 0.001;

	float3 sky_radiance = 0;
	float3 transmittance = 0;
	GetSkyRadianceToPoint(sky_radiance, transmittance);

	// Debug - Global
	{
		bool terminate = true;
		switch (mPerFrame.mDebugMode)
		{
			case DebugMode_Barycentrics: 			hit_info.mEmission = barycentrics; break;
			case DebugMode_Vertex: 					hit_info.mEmission = vertex; break;
			case DebugMode_Normal: 					hit_info.mEmission = normal * 0.5 + 0.5; break;
			case DebugMode_Albedo: 					hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mAlbedo; break;
			case DebugMode_Reflectance: 			hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mReflectance; break;
			case DebugMode_Emission: 				hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mEmission; break;
			case DebugMode_Roughness: 				hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mRoughness; break;
			case DebugMode_Transmittance:			hit_info.mEmission = transmittance; break;
			case DebugMode_InScattering:			hit_info.mEmission = sky_radiance; break;
			default:								terminate = false; break;
		}

		if (terminate)
		{
			hit_info.mDone = true;
			return hit_info;
		}
	}

	// Debug - Per instance
	if (mPerFrame.mDebugInstanceIndex == sGetInstanceID())
	{
		switch (mPerFrame.mDebugInstanceMode)
		{
			case DebugInstanceMode_Barycentrics: 	hit_info.mEmission = barycentrics; hit_info.mDone = true; return hit_info;										// Barycentrics
			case DebugInstanceMode_Mirror: 			hit_info.mAlbedo = 1; hit_info.mReflectionDirection = reflect(sGetWorldRayDirection(), normal); return hit_info;	// Mirror
			default: break;
		}
	}

	// Material
	{
		hit_info.mAlbedo = InstanceDataBuffer[sGetInstanceID()].mAlbedo;
		hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mEmission * (kEmissionScale * kPreExposure);

		if (dot(hit_info.mEmission, 1) > 0) // no reflection from surface with emission
			hit_info.mDone = true;
	}

	// Lambertian
	{
		if (false)
		{
			// random direction

			float3 random_vector = RandomUnitVector(payload.mRandomState);
			if (dot(normal, random_vector) < 0)
				random_vector = -random_vector;
			hit_info.mReflectionDirection = random_vector;

			// pdf - simple distribution
			float cosine = dot(normal, hit_info.mReflectionDirection);
			hit_info.mScatteringPDF = cosine <= 0 ? 0 : cosine / MATH_PI;
			hit_info.mPDF = 1 / (2 * MATH_PI); // hemisphere
		}
		else
		{
			// random cosine direction

			// onb - build_from_w
			float3 axis[3];
			axis[2] = normal;
			float3 a = (abs(axis[2].x) > 0.9) ? float3(0, 1, 0) : float3(1, 0, 0);
			axis[1] = normalize(cross(axis[2], a));
			axis[0] = cross(axis[2], axis[1]);

			// random
			float3 direction = RandomCosineDirection(payload.mRandomState);

			// onb - local
			hit_info.mReflectionDirection = normalize(direction.x * axis[0] + direction.y * axis[1] + direction.z * axis[2]);

			// pdf - exact distribution - should cancel out
			float cosine = dot(normal, hit_info.mReflectionDirection);
			hit_info.mScatteringPDF = cosine <= 0 ? 0 : cosine / MATH_PI;
			hit_info.mPDF = cosine <= 0 ? 0 : cosine / MATH_PI;
		}
	}

	// Sun light
	if (false)
	{
		float sun_light_pdf = 0.01; // sun size over sphere?
		float sun_light_weight = 0.5;
		if (RandomFloat01(payload.mRandomState) < sun_light_weight)
		{
			hit_info.mReflectionDirection = GetSunDirection(); // should be random on sun disk to get soft shadow
			hit_info.mPDF = (1 - sun_light_weight) * hit_info.mPDF + sun_light_weight * sun_light_pdf;

			// how to feedback if sun is not hit?
		}
	}

	// Participating Media
	{
		hit_info.mInScattering = RadianceToLuminance(sky_radiance);
		hit_info.mTransmittance = transmittance;
	}

	return hit_info;
}

[shader("closesthit")]
void DefaultClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	HitInfo hit_info = HitInternal(payload, attributes);

	// State
	payload.mDone = hit_info.mDone;

	// Geometry
	payload.mPosition = hit_info.mPosition;
	payload.mReflectionDirection = hit_info.mReflectionDirection;

	// Material
	payload.mEmission = payload.mEmission + payload.mThroughput * hit_info.mTransmittance * hit_info.mEmission + hit_info.mInScattering;
	payload.mThroughput = hit_info.mPDF <= 0 ? 0 : payload.mThroughput * hit_info.mAlbedo * hit_info.mTransmittance * hit_info.mScatteringPDF / hit_info.mPDF;
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	payload.mHit = true;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
	payload.mHit = false;
}

void TraceRay()
{
	uint3 launchIndex = sGetDispatchRaysIndex();
	uint3 launchDim = sGetDispatchRaysDimensions();

	float2 crd = float2(launchIndex.xy);
	float2 dims = float2(launchDim.xy);

	float2 d = ((crd/dims) * 2.f - 1.f); // 0~1 => -1~1
	d.y = -d.y;
	
	RayDesc ray;
	ray.Origin = mPerFrame.mCameraPosition.xyz;
	ray.Direction = normalize(mPerFrame.mCameraDirection.xyz + mPerFrame.mCameraRightExtend.xyz * d.x + mPerFrame.mCameraUpExtend.xyz * d.y);
	ray.TMin = 0.001;				// Near
	ray.TMax = 100000;				// Far

	RayPayload payload = (RayPayload)0;
	payload.mThroughput = 1; // Camera gather all the light
	payload.mRandomState = uint(uint(sGetDispatchRaysIndex().x) * uint(1973) + uint(sGetDispatchRaysIndex().y) * uint(9277) + uint(mPerFrame.mAccumulationFrameCount) * uint(26699)) | uint(1); // From https://www.shadertoy.com/view/tsBBWW

#ifdef ENABLE_INLINE_RAYTRACING
	// Note that RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH will give first hit for "Any Hit". The result may not be the closest one
	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	uint additional_ray_flags = 0;
	uint ray_instance_mask = 0xffffffff;
#endif // ENABLE_INLINE_RAYTRACING

	uint recursion = 0;
	for (;;)
	{
#ifdef ENABLE_INLINE_RAYTRACING
		sWorldRayOrigin 		= ray.Origin;
		sWorldRayDirection		= ray.Direction;

		query.TraceRayInline(RaytracingScene, additional_ray_flags, ray_instance_mask, ray);
		query.Proceed();

		if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			sRayTCurrent		= query.CommittedRayT();

			sInstanceIndex 		= query.CommittedInstanceIndex();
			sPrimitiveIndex 	= query.CommittedPrimitiveIndex();
			sGeometryIndex 		= query.CommittedGeometryIndex();
			sInstanceID 		= query.CommittedInstanceID();

			BuiltInTriangleIntersectionAttributes attributes;
			attributes.barycentrics = query.CommittedTriangleBarycentrics();

			DefaultClosestHit(payload, attributes);
		}
		else
		{
			DefaultMiss(payload);
		}
#else
		TraceRay(
			RaytracingScene, 		// RaytracingAccelerationStructure
			0,						// RayFlags 
			0xFF,					// InstanceInclusionMask
			0,						// RayContributionToHitGroupIndex, 4bits
			0,						// MultiplierForGeometryContributuionToHitGroupIndex, 16bits
			0,						// MissShaderIndex
			ray,					// RayDesc
			payload					// payload_t
		);
#endif // ENABLE_INLINE_RAYTRACING

		if (payload.mDone)
			break;

		ray.Origin = payload.mPosition;
		ray.Direction = payload.mReflectionDirection;

		// Russian Roulette
		// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting.html
		// https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer
		if (recursion >= mPerFrame.mRecursionCountMax)
		{
			if (mPerFrame.mRecursionMode == RecursionMode_RussianRoulette && recursion <= 8)
			{
				// Probability can be chosen in almost any manner
				// e.g. Fixed threshold
				// e.g. Veach's Efficiency-Optimized Russian roulette is based on average variance and cost
				// Based on throughput here (basically albedo)
				float3 throughput = payload.mThroughput;
				float termination_probability = max(0.25, 1.0 - max(throughput.x, max(throughput.y, throughput.z)));

				if (RandomFloat01(payload.mRandomState) < termination_probability)
					break;

				// Weight the sample to keep result unbiased
				payload.mThroughput /= (1 - termination_probability);
			}
			else
				break;
		}

		recursion++;
	}

	// [Debug]
	// payload.mEmission = RemoveSRGBCurve(payload.mEmission);
	// payload.mEmission = (uint3)(RemoveSRGBCurve(payload.mEmission) * 255.0) / 255.0;

	float3 scene_luminance = payload.mEmission;
	float3 output = LuminanceToColor(scene_luminance);

	if (mPerFrame.mOutputLuminance)
		output = scene_luminance;

	if (mPerFrame.mDebugMode != DebugMode_None)
		output = scene_luminance;

	// Accumulation
	{
		float3 current_output = output;

		float3 previous_output = RaytracingOutput[sGetDispatchRaysIndex().xy].xyz;
		previous_output = max(0, previous_output); // Eliminate nan
		float3 mixed_output = lerp(previous_output, current_output, 1.0f / (float)(mPerFrame.mAccumulationFrameCount));

		if (recursion == 0)
			mixed_output = current_output;

		if (mPerFrame.mDebugMode == DebugMode_RecursionCount)
			mixed_output = hsv2rgb(float3(recursion * 1.0 / (mPerFrame.mRecursionCountMax + 1), 1, 1));

		if (mPerFrame.mDebugMode == DebugMode_RussianRouletteCount)
			mixed_output = hsv2rgb(float3(max(0.0, (recursion * 1.0 - mPerFrame.mRecursionCountMax * 1.0)) / 10.0 /* for visualization only */, 1, 1));

		// [TODO] Ray visualization ?
		// if (all(abs((int2)sGetDispatchRaysIndex().xy - (int2)mDebugCoord) < DEBUG_PIXEL_RADIUS))
		// 	mixed_output = 0;

		output = mixed_output;
	}

	RaytracingOutput[sGetDispatchRaysIndex().xy] = float4(output, 1);
}

[shader("raygeneration")]
void DefaultRayGeneration()
{
	TraceRay();
}

#ifdef ENABLE_INLINE_RAYTRACING
[numthreads(8, 8, 1)]
void InlineRaytracingCS(	
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	uint2 output_dimensions;
	RaytracingOutput.GetDimensions(output_dimensions.x, output_dimensions.y);

	sDispatchRaysIndex.xyz = inDispatchThreadID.xyz;
	sDispatchRaysDimensions = uint3(output_dimensions.xy, 1);

	TraceRay();
}
#endif // ENABLE_INLINE_RAYTRACING

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float4 ScreenspaceTriangleVS(uint id : SV_VertexID) : SV_POSITION
{
	// From https://anteru.net/blog/2012/minimal-setup-screen-space-quads-no-buffers-layouts-required/
	// Generate screen space triangle
	float x = float ((id & 2) << 1) - 1.0;
	float y = 1.0 - float ((id & 1) << 2);
	return float4 (x, y, 0, 1);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Texture2D<float4> InputTexture : register(t0, space1);
[RootSignature("DescriptorTable(CBV(b0, numDescriptors = 1, space = 0), SRV(t0, numDescriptors = 1, space = 1))")]
float4 CompositePS(float4 position : SV_POSITION) : SV_TARGET
{
	float3 color = InputTexture.Load(int3(position.xy, 0)).xyz;

	if (mPerFrame.mOutputLuminance)
		color = LuminanceToColor(color /* as luminance */);

	float3 srgb_color = ApplySRGBCurve(color);
	return float4(srgb_color, 1);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RWTexture2D<float4> InputUAV : register(u0, space1);
RWTexture3D<float4> OutputUAV : register(u1, space1);
[RootSignature("DescriptorTable(UAV(u0, space = 1, numDescriptors = 2))")]
[numthreads(8, 8, 1)]
void CloudShapeNoiseCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	float3 xyz = DispatchThreadID_to_XYZ(inDispatchThreadID.xyz, OutputUAV);

	uint2 coords = inDispatchThreadID.xy;
	coords.x += inDispatchThreadID.z * 128;

	float4 input = InputUAV[coords.xy];
	OutputUAV[inDispatchThreadID.xyz] = pow(input, 1);
}

[RootSignature("DescriptorTable(UAV(u0, space = 1, numDescriptors = 2))")]
[numthreads(8, 8, 1)]
void CloudErosionNoiseCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	float3 xyz = DispatchThreadID_to_XYZ(inDispatchThreadID.xyz, OutputUAV);

	uint2 coords = inDispatchThreadID.xy;
	coords.x += inDispatchThreadID.z * 32;

	float4 input = InputUAV[coords.xy];
	OutputUAV[inDispatchThreadID.xyz] = pow(input, 1);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

[RootSignature("DescriptorTable(UAV(u0, space = 1, numDescriptors = 2))")] 
[numthreads(8, 8, 1)]
void DDGIPrecompute(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// float3 xyz = DispatchThreadID_to_XYZ(inDispatchThreadID.xyz, OutputUAV);
}
