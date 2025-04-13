#include "Shared.h"
#include "Context.h"
#include "Planet.h"

AtmosphereMode GetAtmosphereMode()
{
#ifdef kAtmosphereMode
	// Static
	return kAtmosphereMode;
#else
	// Dynamic
	return mConstants.mAtmosphere.mMode;
#endif // kAtmosphereMode
}

// Altitude -> Density
float GetLayerDensity(DensityProfileLayer layer, float altitude)
{
	float density = layer.mExpTerm * exp(layer.mExpScale * altitude) + layer.mLinearTerm * altitude + layer.mConstantTerm;
	return clamp(density, 0.0, 1.0);
}

// Altitude -> Density
float GetProfileDensity(DensityProfile profile, float altitude)
{
	if (altitude < profile.mLayer0.mWidth)
		return GetLayerDensity(profile.mLayer0, altitude);
	else
		return GetLayerDensity(profile.mLayer1, altitude);
}

float invlerp(float a, float b, float x)
{
	return (x - a) / (b - a);
}

float ClampCosine(float x)
{
	return clamp(x, -1.0, 1.0);
}

float ClampDistance(float d) 
{
	return max(d, 0.0);
}

float ClampRadius(float r) 
{
	return clamp(r, mConstants.mAtmosphere.mBottomRadius, mConstants.mAtmosphere.mTopRadius);
}

// r, mu -> d
float DistanceToTopAtmosphereBoundary(float r, float mu)
{
	float R_t = mConstants.mAtmosphere.mTopRadius;

	float discriminant = r * r * (mu * mu - 1.0) + R_t * R_t;
	return ClampDistance(-r * mu + safe_sqrt(discriminant));
}

// r, d -> mu
float InvDistanceToTopAtmosphereBoundary(float r, float d)
{
	if (d == 0)
		return 1.0;

	float R_t = mConstants.mAtmosphere.mTopRadius;
	return (R_t * R_t - r * r - d * d) / (2.0 * r * d);

	// [Bruneton17] R_t * R_t - r * r == H * H - rho * rho
	// float mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
}

// r, mu -> d
float DistanceToBottomAtmosphereBoundary(float r, float mu)
{
	float R_g = mConstants.mAtmosphere.mBottomRadius;

	float discriminant = r * r * (mu * mu - 1.0) + R_g * R_g;
	return ClampDistance(-r * mu - safe_sqrt(discriminant));
}

// r, mu -> d
float DistanceToNearestAtmosphereBoundary(float r, float mu, bool intersects_ground)
{
	if (intersects_ground) 
		return DistanceToBottomAtmosphereBoundary(r, mu);
	else
		return DistanceToTopAtmosphereBoundary(r, mu);
}

// r, mu -> bool
bool RayIntersectsGround(float r, float mu) 
{
	return mu < 0.0 && (r * r * (mu * mu - 1.0) + mConstants.mAtmosphere.mBottomRadius * mConstants.mAtmosphere.mBottomRadius) >= 0.0;
}

#define USE_HALF_PIXEL_OFFSET
float2 DispatchThreadID_to_XY(uint2 inDispatchThreadID, RWTexture2D<float4> inTexture)
{
	uint2 size;
	inTexture.GetDimensions(size.x, size.y);
#ifdef USE_HALF_PIXEL_OFFSET
	return (inDispatchThreadID.xy + 0.5) / size;
#else
	return (inDispatchThreadID.xy) / (size - 1.0);
#endif // USE_HALF_PIXEL_OFFSET
}

float3 DispatchThreadID_to_XYZ(uint3 inDispatchThreadID, RWTexture3D<float4> inTexture)
{
	uint3 size;
	inTexture.GetDimensions(size.x, size.y, size.z);
#ifdef USE_HALF_PIXEL_OFFSET
	return (inDispatchThreadID.xyz + 0.5) / size;
#else
	return (inDispatchThreadID.xyz) / (size - 1.0);
#endif // USE_HALF_PIXEL_OFFSET
}

float X_to_U(float x, uint size)
{
	return 0.5 / size + x * (1.0 - 1.0 / size);
}

float U_to_X(float u, uint size)
{
	return (u - 0.5 / size) / (1.0 - 1.0 / size);
}

float2 XY_to_UV(float2 xy, Texture2D<float4> texture) // GetTextureCoordFromUnitRange
{
	uint2 size;
	texture.GetDimensions(size.x, size.y);
	return float2(X_to_U(xy.x, size.x), X_to_U(xy.y, size.y));
}

float2 UV_to_XY(float2 uv, Texture2D<float4> texture) // GetUnitRangeFromTextureCoord
{
	uint2 size;
	texture.GetDimensions(size.x, size.y);
	return float2(U_to_X(uv.x, size.x), U_to_X(uv.y, size.y));
}

#include "AtmosphereIntegration.Bruneton17.h"
#include "AtmosphereIntegration.Hillaire20.h"
#include "AtmosphereIntegration.Wilkie21.h"
#include "AtmosphereIntegration.Raymarch.h"

void GetSunAndSkyIrradiance(float3 inHitPosition, float3 inNormal, out float3 outSunIrradiance, out float3 outSkyIrradiance)
{
	outSunIrradiance = 0;
	outSkyIrradiance = 0;

	switch (GetAtmosphereMode())
	{
	case AtmosphereMode::ConstantColor:				break; // Not supported
	case AtmosphereMode::Raymarch:					break; // Not supported
	case AtmosphereMode::Bruneton17: 				AtmosphereIntegration::Bruneton17::GetSunAndSkyIrradiance(inHitPosition, inNormal, outSunIrradiance, outSkyIrradiance); break;
	case AtmosphereMode::Hillaire20: 				break; // [TODO]
	default: break;
	}
}

void GetSkyRadiance(Ray inRayPS, out float3 outSkyRadiance, out float3 outTransmittanceToTop)
{
	outSkyRadiance = 0;
	outTransmittanceToTop = 1;

	switch (GetAtmosphereMode())
	{
	case AtmosphereMode::ConstantColor:				outSkyRadiance = mConstants.mAtmosphere.mConstantColor.xyz; break;
	case AtmosphereMode::Wilkie21:					AtmosphereIntegration::Wilkie21::GetSkyRadiance(inRayPS, outSkyRadiance, outTransmittanceToTop); break;
	case AtmosphereMode::Raymarch:					AtmosphereIntegration::Raymarch::GetSkyRadiance(inRayPS, outSkyRadiance, outTransmittanceToTop); break;
	case AtmosphereMode::Bruneton17: 				AtmosphereIntegration::Bruneton17::GetSkyRadiance(inRayPS, outSkyRadiance, outTransmittanceToTop); break;
	case AtmosphereMode::Hillaire20: 				AtmosphereIntegration::Hillaire20::GetSkyRadiance(inRayPS, outSkyRadiance, outTransmittanceToTop); break;
	default: break;
	}
}

void GetSkyLuminanceToPoint(Ray inRayWS, out float3 outSkyLuminance, out float3 outTransmittance)
{
	outSkyLuminance = 0;
	outTransmittance = 1;

	if (mConstants.mAtmosphere.mAerialPerspective == 0)
		return;

	float3 radiance = 0;
	Ray ray_PS = RayWS2PS(inRayWS);
	switch (GetAtmosphereMode())
	{
	case AtmosphereMode::ConstantColor:				break; // Not supported
	case AtmosphereMode::Raymarch:					break; // Not supported
	case AtmosphereMode::Bruneton17: 				AtmosphereIntegration::Bruneton17::GetSkyRadianceToPoint(ray_PS, radiance, outTransmittance); break;
	case AtmosphereMode::Hillaire20: 				break; // [TODO]
	default: break;
	}

	outSkyLuminance = radiance * kSolarKW2LM * kPreExposure * mConstants.mSolarLuminanceScale;
}

float3 GetSkyLuminance(Ray inRayWS)
{
	// Sky
	float3 radiance = 0;
	float3 transmittance_to_top = 0;
	Ray ray_PS = RayWS2PS(inRayWS);
	GetSkyRadiance(ray_PS, radiance, transmittance_to_top);

	// Debug
	switch (mConstants.mVisualizeMode)
	{
	case VisualizeMode::Barycentrics: 			return 0;
	case VisualizeMode::Position: 				return ray_PS.mDirection; // rays are supposed to go infinity
	case VisualizeMode::Normal: 				return -ray_PS.mDirection;
	case VisualizeMode::Albedo: 				return 0;
	case VisualizeMode::Reflectance: 			return 0;
	case VisualizeMode::Emission: 				return 0;
	case VisualizeMode::RoughnessAlpha:			return 0;
	case VisualizeMode::Transmittance:			return transmittance_to_top;
	case VisualizeMode::InScattering:			return radiance;
	default:								break;
	}

	// Sun
	if (GetAtmosphereMode() != AtmosphereMode::ConstantColor &&
		dot(ray_PS.mDirection, GetSunDirection()) > cos(mConstants.mAtmosphere.mSunAngularRadius))
	{
		// https://en.wikipedia.org/wiki/Solid_angle#Celestial_objects
		// https://pages.mtu.edu/~scarn/teaching/GE4250/radiation_lecture_slides.pdf
		// static const float kSunSolidAngle = 6.8E-5;
		static const float kSunSolidAngle = 6.8E-5;
		static const float kSolarRadianceScale = 1E-5; // Limit radiance to prevent fireflies...		
		float3 solar_radiance = mConstants.mAtmosphere.mSolarIrradiance * mConstants.mAtmosphere.mSunDiskLuminanceScale / kSunSolidAngle * kSolarRadianceScale;
		radiance = radiance + transmittance_to_top * solar_radiance;
	}

	return radiance * kSolarKW2LM * kPreExposure * mConstants.mSolarLuminanceScale;
}