#include "Common.hlsl"

typedef uint DebugMode;
typedef uint DebugInstanceMode;
typedef uint BackgroundMode;
#define CONSTANT_DEFAULT(x)
#include "ShaderType.hlsl"
#include "Generated/Enum.hlsl"

//////////////////////////////////////////////////////////////////////////////////

RWTexture2D<float4> RaytracingOutput : register(u0, space0);
cbuffer PerFrameBuffer : register(b0, space0)
{
	PerFrame mPerFrame;
};

SamplerState PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

//////////////////////////////////////////////////////////////////////////////////

#include "PrecomputedAtmosphere.hlsl"

//////////////////////////////////////////////////////////////////////////////////

RaytracingAccelerationStructure RaytracingScene : register(t0, space0);
StructuredBuffer<InstanceData> InstanceDataBuffer : register(t1, space0);
ByteAddressBuffer Indices : register(t2, space0);
StructuredBuffer<float3> Vertices : register(t3, space0);
StructuredBuffer<float3> Normals : register(t4, space0);

#define DEBUG_PIXEL_RADIUS (3)

// From D3D12Raytracing
// Load three 16 bit indices from a byte addressed buffer.
uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;
    const uint2 four16BitIndices = Indices.Load2(dwordAlignedOffset);

    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

uint3 Load3x32BitIndices(uint offsetBytes)
{
	const uint4 four32BitIndices = Indices.Load4(offsetBytes);
	return four32BitIndices.xyz;
}

[shader("raygeneration")]
void DefaultRayGeneration()
{
	uint3 launchIndex = DispatchRaysIndex();
	uint3 launchDim = DispatchRaysDimensions();

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
	payload.mAlbedo = 1; // camera gather 100% light
	payload.mRandomState = uint(uint(DispatchRaysIndex().x) * uint(1973) + uint(DispatchRaysIndex().y) * uint(9277) + uint(mPerFrame.mAccumulationFrameCount) * uint(26699)) | uint(1); // From https://www.shadertoy.com/view/tsBBWW

	uint recursion = 0;
	for (; recursion <= mPerFrame.mRecursionCountMax; recursion++)
	{
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

		if (payload.mDone)
			break;

		ray.Origin = payload.mPosition;
		ray.Direction = payload.mReflectionDirection;
	}

	// [Debug]
	// payload.mEmission = RemoveSRGBCurve(payload.mEmission);
	// payload.mEmission = (uint3)(RemoveSRGBCurve(payload.mEmission) * 255.0) / 255.0;

	float3 current_frame_color = payload.mEmission;
	float3 previous_frame_color = RaytracingOutput[DispatchRaysIndex().xy].xyz;
	previous_frame_color = max(0, previous_frame_color); // Eliminate nan
	float3 mixed_color = lerp(previous_frame_color, current_frame_color, 1.0f / (float)(mPerFrame.mAccumulationFrameCount));

	if (mPerFrame.mDebugMode == DebugMode_RecursionCount)
		mixed_color = hsv2rgb(float3(recursion * 1.0 / (mPerFrame.mRecursionCountMax + 1), 1, 1));

	// [TODO] Ray visualization ?
	// if (all(abs((int2)DispatchRaysIndex().xy - (int2)mDebugCoord) < DEBUG_PIXEL_RADIUS))
	// 	mixed_color = 0;

	RaytracingOutput[DispatchRaysIndex().xy] = float4(mixed_color, 1);
}

#include "AtmosphericScattering.hlsl"

// Adapters
float3 RayOrigin() { return WorldRayOrigin() * mAtmosphere.mSceneScale; }
float3 RayDirection() { return WorldRayDirection(); }
float3 RayHitPosition() { return (WorldRayOrigin() + WorldRayDirection() * RayTCurrent()) * mAtmosphere.mSceneScale; }
float3 PlanetCenter() { return float3(0, -mAtmosphere.mBottomRadius, 0); }
float PlanetRadius() { return mAtmosphere.mBottomRadius; }
float3 GetSunDirection() { return mPerFrame.mSunDirection.xyz; }

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

	transmittance_to_top = ray_r_mu_intersects_ground ? 0 : GetTransmittanceToTopAtmosphereBoundary(r, mu);
	float3 single_mie_scattering;
	float3 scattering;

	// [TODO] shadow

	scattering = GetCombinedScattering(r, mu, mu_s, nu, ray_r_mu_intersects_ground, single_mie_scattering);

	// [TODO] light shafts

	sky_radiance = scattering * RayleighPhaseFunction(nu) + single_mie_scattering * MiePhaseFunction(mAtmosphere.mMiePhaseFunctionG, nu);
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

	// Compute the r, mu, mu_s and nu parameters for the first texture lookup.
	float mu = rmu / r;
	float mu_s = dot(camera, sun_direction) / r;
	float nu = dot(view_ray, sun_direction);
	float d = length(hit_position - camera);
	bool ray_r_mu_intersects_ground = RayIntersectsGround(r, mu);

	transmittance = GetTransmittance(r, mu, d, ray_r_mu_intersects_ground);

	float3 single_mie_scattering;
	float3 scattering = GetCombinedScattering(r, mu, mu_s, nu, ray_r_mu_intersects_ground, single_mie_scattering);

	// [TODO] shadow
	float shadow_length = 0;
	float3 single_mie_scattering_p = single_mie_scattering;
	float3 scattering_p = scattering;
	if (shadow_length > 0)
	{
		// Compute the r, mu, mu_s and nu parameters for the second texture lookup.
		// If shadow_length is not 0 (case of light shafts), we want to ignore the
		// scattering along the last shadow_length meters of the view ray, which we
		// do by subtracting shadow_length from d (this way scattering_p is equal to
		// the S|x_s=x_0-lv term in Eq. (17) of our paper).
		d = max(d - shadow_length, 0.0);
		float r_p = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));
		float mu_p = (r * mu + d) / r_p;
		float mu_s_p = (r * mu_s + d * nu) / r_p;

		scattering_p = GetCombinedScattering(r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground, single_mie_scattering_p);
	}

	// Combine the lookup results to get the scattering between camera and point.
	float3 shadow_transmittance = transmittance;
	if (shadow_length > 0.0)
	{
		// This is the T(x,x_s) term in Eq. (17) of our paper, for light shafts.
		shadow_transmittance = GetTransmittance(r, mu, d, ray_r_mu_intersects_ground);
	}
	scattering = scattering - shadow_transmittance * scattering_p;
	single_mie_scattering = single_mie_scattering - shadow_transmittance * single_mie_scattering_p;

	single_mie_scattering = GetExtrapolatedSingleMieScattering(float4(scattering.rgb, single_mie_scattering.r));

	// Hack to avoid rendering artifacts when the sun is below the horizon.
	single_mie_scattering = single_mie_scattering * smoothstep(float(0.0), float(0.01), mu_s);

	sky_radiance = scattering * RayleighPhaseFunction(nu) + single_mie_scattering * MiePhaseFunction(mAtmosphere.mMiePhaseFunctionG, nu);
}

void GetSunAndSkyIrradiance(float3 hit_position, float3 normal, out float3 sun_irradiance, out float3 sky_irradiance)
{
	float3 local_position = hit_position - PlanetCenter();
	float3 sun_direction = GetSunDirection();

	float r = length(local_position);
	float mu_s = dot(local_position, sun_direction) / r;

	// Indirect irradiance (approximated if the surface is not horizontal).
	sky_irradiance = GetIrradiance(r, mu_s) * (1.0 + dot(normal, local_position) / r) * 0.5;

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
		if (mPerFrame.mDebugMode != DebugMode_None && mPerFrame.mDebugMode != DebugMode_RecursionCount)
		{
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
			default:
				break;
			}
		}

		// Blend
		if (mAtmosphere.mAerialPerspective == 0)
			radiance = ground_radiance;
		else
			radiance = radiance + transmittance_to_ground * ground_radiance;
	}

	// Sun
	if (dot(RayDirection(), GetSunDirection()) > cos(mAtmosphere.mSunAngularRadius))
	{
		radiance = radiance + transmittance_to_top * mAtmosphere.mSolarIrradiance;
	}

	// [TODO]
	float3 white_point = float3(1, 1, 1);
	float exposure = 10.0;
	return 1 - exp(-radiance / white_point * exposure);
}

[shader("miss")]
void DefaultMiss(inout RayPayload payload)
{
	payload.mDone = true;

	switch (mPerFrame.mBackgroundMode)
	{
		default:
		case BackgroundMode_Color: 					payload.mEmission = payload.mEmission + payload.mAlbedo * mPerFrame.mBackgroundColor.xyz; return;
		case BackgroundMode_Atmosphere: 			payload.mEmission = payload.mEmission + payload.mAlbedo * AtmosphereScattering(RayOrigin(), RayDirection()); return;
		case BackgroundMode_PrecomputedAtmosphere: 	payload.mEmission = payload.mEmission + payload.mAlbedo * GetEnvironmentEmission(); return;
	}
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

	bool use_16bit_index = false;

	// Get the base index of the triangle's first 16 or 32 bit index.
	uint index_size_in_bytes = use_16bit_index ? 2 : 4;
	uint index_count_per_triangle = 3;
	uint triangle_index_stride = index_count_per_triangle * index_size_in_bytes;
	uint base_index = PrimitiveIndex() * triangle_index_stride + InstanceDataBuffer[InstanceID()].mIndexOffset * index_size_in_bytes;

	// Load up 3 16 bit indices for the triangle.
	uint3 indices = use_16bit_index ? Load3x16BitIndices(base_index) : Load3x32BitIndices(base_index);

    // Attributes
    float3 normals[3] = { Normals[indices[0]], Normals[indices[1]], Normals[indices[2]] };
    float3 normal = normalize(normals[0] * barycentrics.x + normals[1] * barycentrics.y + normals[2] * barycentrics.z);

    float3 vertices[3] = { Vertices[indices[0]], Vertices[indices[1]], Vertices[indices[2]] };
    float3 vertex = vertices[0] * barycentrics.x + vertices[1] * barycentrics.y + vertices[2] * barycentrics.z;

    // Handle front face only
	if (dot(normal, -WorldRayDirection()) < 0)
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
	float3 raw_hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
	hit_info.mPosition = raw_hit_position + normal * 0.001;

	float3 sky_radiance = 0;
	float3 transmittance = 0;
	GetSkyRadianceToPoint(sky_radiance, transmittance);

    // Debug - Global
	if (mPerFrame.mDebugMode != DebugMode_None && mPerFrame.mDebugMode != DebugMode_RecursionCount)
	{
		switch (mPerFrame.mDebugMode)
	    {
	    	case DebugMode_Barycentrics: 			hit_info.mEmission = barycentrics; break;
	    	case DebugMode_Vertex: 					hit_info.mEmission = vertex; break;
	    	case DebugMode_Normal: 					hit_info.mEmission = normal * 0.5 + 0.5; break;
	    	case DebugMode_Albedo: 					hit_info.mEmission = InstanceDataBuffer[InstanceID()].mAlbedo; break;
	    	case DebugMode_Reflectance: 			hit_info.mEmission = InstanceDataBuffer[InstanceID()].mReflectance; break;
	    	case DebugMode_Emission: 				hit_info.mEmission = InstanceDataBuffer[InstanceID()].mEmission; break;
	    	case DebugMode_Roughness: 				hit_info.mEmission = InstanceDataBuffer[InstanceID()].mRoughness; break;
			case DebugMode_Transmittance:			hit_info.mEmission = transmittance; break;
			case DebugMode_InScattering:			hit_info.mEmission = sky_radiance; break;
	    	default:
	    		break;
	    }

	    hit_info.mDone = true;
	    return hit_info;
	}

	// Debug - Per instance
	if (mPerFrame.mDebugInstanceIndex == InstanceID())
	{
		switch (mPerFrame.mDebugInstanceMode)
		{
			case DebugInstanceMode_Barycentrics: 	hit_info.mEmission = barycentrics; hit_info.mDone = true; return hit_info;										// Barycentrics
			case DebugInstanceMode_Mirror: 			hit_info.mAlbedo = 1; hit_info.mReflectionDirection = reflect(WorldRayDirection(), normal); return hit_info;	// Mirror
			default: break;
		}
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
		
		hit_info.mAlbedo = InstanceDataBuffer[InstanceID()].mAlbedo;
		hit_info.mEmission = InstanceDataBuffer[InstanceID()].mEmission;
	}

	// Participating Media
	{
		// [TODO]
		float3 white_point = float3(1, 1, 1);
		float exposure = 10.0;
		hit_info.mInScattering = 1 - exp(-sky_radiance / white_point * exposure);
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
	payload.mEmission = payload.mEmission + payload.mAlbedo * hit_info.mTransmittance * hit_info.mEmission + hit_info.mInScattering;
	bool use_pdf = true;
	if (use_pdf)
		payload.mAlbedo = hit_info.mPDF <= 0 ? 0 : payload.mAlbedo * hit_info.mAlbedo * hit_info.mTransmittance * hit_info.mScatteringPDF / hit_info.mPDF;
	else
		payload.mAlbedo = payload.mAlbedo * hit_info.mAlbedo * hit_info.mTransmittance;
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

Texture2D<float4> CopyFromTexture : register(t0, space1);
[RootSignature("DescriptorTable(SRV(t0, numDescriptors = 1, space = 1))")]
float4 CompositePS(float4 position : SV_POSITION) : SV_TARGET
{
	float3 srgb = ApplySRGBCurve(CopyFromTexture.Load(int3(position.xy, 0)).xyz);
	return float4(srgb, 1);
}
