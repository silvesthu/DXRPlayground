cbuffer AtmosphereConstantsPerDrawBuffer : register(b1, space2)
{
	AtmosphereConstantsPerDraw mAtmospherePerDraw;
}

RWTexture2D<float4> TransmittanceUAV								: register(u0, space2);
RWTexture2D<float4> DeltaIrradianceUAV								: register(u1, space2);
RWTexture2D<float4> IrradianceUAV									: register(u2, space2);
RWTexture3D<float4> DeltaRayleighScatteringUAV						: register(u3, space2);
RWTexture3D<float4> DeltaMieScatteringUAV							: register(u4, space2);
RWTexture3D<float4> ScatteringUAV									: register(u5, space2);
RWTexture3D<float4> DeltaScatteringDensityUAV						: register(u6, space2);
RWTexture2D<float4> TransmittanceTexUAV								: register(u7, space2);
RWTexture2D<float4> MultiScattTexUAV								: register(u8, space2);
RWTexture2D<float4> SkyViewLutTexUAV								: register(u9, space2);
RWTexture3D<float4> AtmosphereCameraScatteringVolumeUAV				: register(u10, space2);

Texture2D<float4> TransmittanceSRV									: register(t0, space2);
Texture2D<float4> DeltaIrradianceSRV								: register(t1, space2);
Texture2D<float4> IrradianceSRV										: register(t2, space2);
Texture3D<float4> DeltaRayleighScatteringSRV						: register(t3, space2);
Texture3D<float4> DeltaMieScatteringSRV								: register(t4, space2);
Texture3D<float4> ScatteringSRV										: register(t5, space2);
Texture3D<float4> DeltaScatteringDensitySRV							: register(t6, space2);
Texture2D<float4> TransmittanceTexSRV								: register(t7, space2);
Texture2D<float4> MultiScattTexSRV									: register(t8, space2);
Texture2D<float4> SkyViewLutTexSRV									: register(t9, space2);
Texture3D<float4> AtmosphereCameraScatteringVolumeSRV				: register(t10, space2);

#define AtmosphereRootSignature							\
"DescriptorTable("										\
	"  CBV(b0, space = 0)"								\
	", CBV(b0, space = 2)"								\
	", UAV(u0, space = 2, numDescriptors = 11)"			\
	", SRV(t0, space = 2, numDescriptors = 11)"			\
")"														\
", RootConstants(num32BitConstants=4, b1, space = 2)"	\
", StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)"	\
", StaticSampler(s1, filter = FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP)"

float invlerp(float a, float b, float x)
{
	return (x - a) / (b - a);
}

float SafeSqrt(float a)
{
	return sqrt(max(a, 0));
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
	return clamp(r, mAtmosphere.mBottomRadius, mAtmosphere.mTopRadius);
}

// r, mu -> d
float DistanceToTopAtmosphereBoundary(float r, float mu)
{
	float R_t = mAtmosphere.mTopRadius;

	float discriminant = r * r * (mu * mu - 1.0) + R_t * R_t;
	return ClampDistance(-r * mu + SafeSqrt(discriminant));
}

// r, d -> mu
float InvDistanceToTopAtmosphereBoundary(float r, float d)
{
	if (d == 0)
		return 1.0;

	float R_t = mAtmosphere.mTopRadius;
	return (R_t * R_t - r * r - d * d) / (2.0 * r * d);

	// [Bruneton17] R_t * R_t - r * r == H * H - rho * rho
	// float mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
}

// r, mu -> d
float DistanceToBottomAtmosphereBoundary(float r, float mu)
{
	float R_g = mAtmosphere.mBottomRadius;

	float discriminant = r * r * (mu * mu - 1.0) + R_g * R_g;
	return ClampDistance(-r * mu - SafeSqrt(discriminant));
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
	return mu < 0.0 && (r * r * (mu * mu - 1.0) + mAtmosphere.mBottomRadius * mAtmosphere.mBottomRadius) >= 0.0;
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

#include "AtmosphereIntegration.Bruneton17.inl"
#include "AtmosphereIntegration.Hillaire20.inl"
#include "AtmosphereIntegration.Raymarch.inl"

void GetSunAndSkyIrradiance(float3 inHitPosition, float3 inNormal, out float3 outSunIrradiance, out float3 outSkyIrradiance)
{
	outSunIrradiance = 0;
	outSkyIrradiance = 0;

	switch (mAtmosphere.mMode)
	{
    case AtmosphereMode::ConstantColor:				break; // Not supported
	case AtmosphereMode::RaymarchAtmosphereOnly:	break; // Not supported
	case AtmosphereMode::Bruneton17: 				AtmosphereIntegration::Bruneton17::GetSunAndSkyIrradiance(inHitPosition, inNormal, outSunIrradiance, outSkyIrradiance); break;
	case AtmosphereMode::Hillaire20: 				break; // [TODO]
	default: break;
	}
}

void GetSkyRadiance(out float3 outSkyRadiance, out float3 outTransmittanceToTop)
{
	outSkyRadiance = 0;
	outTransmittanceToTop = 1;

	switch (mAtmosphere.mMode)
	{
	case AtmosphereMode::ConstantColor:				outSkyRadiance = mAtmosphere.mConstantColor.xyz; break;
	case AtmosphereMode::RaymarchAtmosphereOnly:	AtmosphereIntegration::Raymarch::GetSkyRadiance(outSkyRadiance, outTransmittanceToTop); break;
	case AtmosphereMode::Bruneton17: 				AtmosphereIntegration::Bruneton17::GetSkyRadiance(outSkyRadiance, outTransmittanceToTop); break;
	case AtmosphereMode::Hillaire20: 				AtmosphereIntegration::Hillaire20::GetSkyRadiance(outSkyRadiance, outTransmittanceToTop); break;
	default: break;
	}
}

void GetSkyLuminanceToPoint(out float3 outSkyLuminance, out float3 outTransmittance)
{
	outSkyLuminance = 0;
	outTransmittance = 1;

	if (mAtmosphere.mAerialPerspective == 0)
		return;

	float3 radiance = 0;
    switch (mAtmosphere.mMode)
    {
    case AtmosphereMode::ConstantColor:				break; // Not supported
	case AtmosphereMode::RaymarchAtmosphereOnly:	break; // Not supported
    case AtmosphereMode::Bruneton17: 				AtmosphereIntegration::Bruneton17::GetSkyRadianceToPoint(radiance, outTransmittance); break;
    case AtmosphereMode::Hillaire20: 				break; // [TODO]
    default: break;
    }

	outSkyLuminance = RadianceToLuminance(radiance) * mPerFrameConstants.mSunLuminanceScale;
}

float3 GetSkyLuminance()
{
	// Sky
	float3 radiance = 0;
	float3 transmittance_to_top = 0;
	GetSkyRadiance(radiance, transmittance_to_top);

	// Debug
	switch (mPerFrameConstants.mDebugMode)
	{
	case DebugMode::Barycentrics: 			return 0;
	case DebugMode::Vertex: 				return PlanetRayDirection(); // rays are supposed to go infinity
	case DebugMode::Normal: 				return -PlanetRayDirection();
	case DebugMode::Albedo: 				return 0;
	case DebugMode::Reflectance: 			return 0;
	case DebugMode::Emission: 				return 0;
	case DebugMode::Roughness: 				return 1;
	case DebugMode::Transmittance:			return transmittance_to_top;
	case DebugMode::InScattering:			return radiance;
	default:								break;
	}

	// Sun
	if (dot(PlanetRayDirection(), GetSunDirection()) > cos(mAtmosphere.mSunAngularRadius))
	{
		radiance = radiance + transmittance_to_top * mAtmosphere.mSolarIrradiance;
	}

	return RadianceToLuminance(radiance) * mPerFrameConstants.mSunLuminanceScale;
}