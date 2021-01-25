#include "Common.hlsl"

typedef uint DebugMode;
typedef uint DebugInstanceMode;
typedef uint BackgroundMode;
#define CONSTANT_DEFAULT(x)
#include "ShaderType.hlsl"
#include "Generated/Enum.hlsl"

RWTexture2D<float4> RaytracingOutput : register(u0, space0);
cbuffer PerFrameBuffer : register(b0, space0)
{
    PerFrame mPerFrame;
}

RaytracingAccelerationStructure RaytracingScene : register(t0, space0);
StructuredBuffer<InstanceData> InstanceDataBuffer : register(t1, space0);
ByteAddressBuffer Indices : register(t2, space0);
StructuredBuffer<float3> Vertices : register(t3, space0);
StructuredBuffer<float3> Normals : register(t4, space0);

#define DEBUG_PIXEL_RADIUS (3)

// From D3D12Raytracing
// Load three 16 bit indices from a byte addressed buffer.
static
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

	float3 current_frame_color = payload.mEmission;
	float3 previous_frame_color = RaytracingOutput[DispatchRaysIndex().xy].xyz;
	float3 mixed_color = lerp(previous_frame_color, current_frame_color, 1.0f / (float)(mPerFrame.mAccumulationFrameCount));

	if (mPerFrame.mDebugMode == DebugMode_RecursionCount)
		mixed_color = hsv2rgb(float3(recursion * 1.0 / (mPerFrame.mRecursionCountMax + 1), 1, 1));

	// [TODO] Ray visualization ?
	// if (all(abs((int2)DispatchRaysIndex().xy - (int2)mDebugCoord) < DEBUG_PIXEL_RADIUS))
	// 	mixed_color = 0;

	RaytracingOutput[DispatchRaysIndex().xy] = float4(mixed_color, 1);
}

#include "AtmosphericScattering.hlsl"

[shader("miss")]
void DefaultMiss(inout RayPayload payload)
{
	payload.mDone = true;

	switch (mPerFrame.mBackgroundMode)
	{
		default:
		case BackgroundMode_Color: 			payload.mEmission = payload.mEmission + payload.mAlbedo * mPerFrame.mBackgroundColor.xyz; return;
		case BackgroundMode_Atmosphere: 	payload.mEmission = payload.mEmission + payload.mAlbedo * AtmosphereScattering(WorldRayOrigin(), WorldRayDirection()); return;
	}
}

HitInfo HitInternal(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	HitInfo hit_info = (HitInfo)0;
	hit_info.mPDF = 1.0;
	hit_info.mScatteringPDF = 1.0;

	// See https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html for more system value intrinsics
	float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);

	// Get the base index of the triangle's first 16 bit index.
    uint index_size_in_bytes = 2;
    uint index_count_per_triangle = 3;
    uint triangleIndexStride = index_count_per_triangle * index_size_in_bytes;
    uint base_index = PrimitiveIndex() * triangleIndexStride + InstanceDataBuffer[InstanceID()].mIndexOffset * index_size_in_bytes;

    // Load up 3 16 bit indices for the triangle.
    const uint3 indices = Load3x16BitIndices(base_index);

    // Attributes
    float3 normals[3] = { Normals[indices[0]], Normals[indices[1]], Normals[indices[2]] };
    float3 normal = normalize(normals[0] * barycentrics.x + normals[1] * barycentrics.y + normals[2] * barycentrics.z);

    float3 vertices[3] = { Vertices[indices[0]], Vertices[indices[1]], Vertices[indices[2]] };
    float3 vertex = vertices[0] * barycentrics.x + vertices[1] * barycentrics.y + vertices[2] * barycentrics.z;

    // Handle front face only
	if (dot(normal, -WorldRayDirection()) < 0)
	{
	    hit_info.mDone = true;
	    return hit_info;
	}

	// Hit position
	float3 raw_hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
	hit_info.mPosition = raw_hit_position + normal * 0.001;

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

	return hit_info;
}

[shader("closesthit")]
void DefaultClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	HitInfo hit_info = HitInternal(payload, attribs);

	// State
	payload.mDone = hit_info.mDone;

	// Geometry
	payload.mPosition = hit_info.mPosition;
	payload.mReflectionDirection = hit_info.mReflectionDirection;

	// Material
	payload.mEmission = payload.mEmission + payload.mAlbedo * hit_info.mEmission;
	bool use_pdf = true;
	if (use_pdf)
		payload.mAlbedo = hit_info.mPDF <= 0 ? 0 : payload.mAlbedo * hit_info.mAlbedo * hit_info.mScatteringPDF / hit_info.mPDF;
	else
		payload.mAlbedo = payload.mAlbedo * hit_info.mAlbedo;	
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// mu
// mu_s
// mu_r
// r = [mAtmosphere.mBottomRadius, mAtmosphere.mTopRadius]

cbuffer AtmosphereBuffer : register(b0, space2)
{
	Atmosphere mAtmosphere;
}
RWTexture2D<float4> TransmittanceUAV : register(u0, space2); // X: [mAtmosphere.mBottomRadius, mAtmosphere.mTopRadius] Y: [?]
RWTexture2D<float4> DeltaIrradianceUAV : register(u1, space2);
RWTexture2D<float4> IrradianceUAV : register(u2, space2);

RWTexture3D<float4> DeltaRayleighScatteringUAV : register(u3, space2);
RWTexture3D<float4> DeltaMieScatteringUAV : register(u4, space2);
RWTexture3D<float4> ScatteringUAV : register(u5, space2);

Texture2D<float4> TransmittanceSRV : register(t0, space2);

SamplerState PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

#define AtmosphereRootSignature					\
"DescriptorTable("								\
	"  CBV(b0, space = 2)"						\
	", UAV(u0, space = 2, numDescriptors = 7)"	\
	", SRV(t0, space = 2, numDescriptors = 7)"	\
")"												\
", StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)"	\
", StaticSampler(s1, filter = FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)"

float DistanceToTopAtmosphereBoundary(float r, float mu)
{
	float discriminant = max(0.0, r * r * (mu * mu - 1.0) + mAtmosphere.mTopRadius * mAtmosphere.mTopRadius);
	return max(0.0, -r * mu + sqrt(discriminant));
}

float invlerp(float a, float b, float x)
{
	return (x - a) / (b - a);
}

float2 DispatchThreadID_to_XY(uint2 inDispatchThreadID, RWTexture2D<float4> inTexture)
{
	uint2 size;
	inTexture.GetDimensions(size.x, size.y);
	return inDispatchThreadID.xy / (size - 1.0);
}

float3 DispatchThreadID_to_XYZ(uint3 inDispatchThreadID, RWTexture3D<float4> inTexture)
{
	uint3 size;
	inTexture.GetDimensions(size.x, size.y, size.z);
	return inDispatchThreadID.xyz / (size - 1.0);
}

float2 XY_to_UV(float2 xy, Texture2D<float4> texture) // GetTextureCoordFromUnitRange
{
	uint2 size;
	texture.GetDimensions(size.x, size.y);
	return 0.5 / size + xy * (1.0 - 1.0 / size);
}

float2 UV_to_XY(float2 uv, Texture2D<float4> texture) // GetUnitRangeFromTextureCoord
{
	uint2 size;
	texture.GetDimensions(size.x, size.y);
	return (uv - 0.5 / size) / (1.0 - 1.0 / size);
}

float2 Decode2D(float2 xy)
{
	float mu = lerp(-1, 1, xy.x);
	float r = lerp(mAtmosphere.mBottomRadius, mAtmosphere.mTopRadius, xy.y);
	return float2(mu, r);
}

float2 Encode2D(float2 mu_r)
{
	float x = invlerp(-1, 1, mu_r.x);
	float y = invlerp(mAtmosphere.mBottomRadius, mAtmosphere.mTopRadius, mu_r.y);
	return float2(x, y);
}

float2 Decode2D_Transmittance(float2 xy)
{
	if (mAtmosphere.mUnifyXYEncode)
		return Decode2D(xy);

	float x_mu = xy.x;
	float x_r = xy.y;

	float H = sqrt(mAtmosphere.mTopRadius * mAtmosphere.mTopRadius - mAtmosphere.mBottomRadius * mAtmosphere.mBottomRadius);
	float rho = H * x_r;
	float r = sqrt(rho * rho + mAtmosphere.mBottomRadius * mAtmosphere.mBottomRadius);
	float d_min = mAtmosphere.mTopRadius - r;
	float d_max = rho + H;
	float d = d_min + x_mu * (d_max - d_min);
	float mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
	mu = clamp(mu, -1.0, 1.0);

	// Annotated ver.
	// if (false)
	{
		// eye ----> p ----> Horizon
		//  ^
		//  |
		//  |
		// center

		// Y -> r = center_to_p_distance where p = [eye, horizon]
		float eye_to_horizon_distance = sqrt(mAtmosphere.mTopRadius * mAtmosphere.mTopRadius - mAtmosphere.mBottomRadius * mAtmosphere.mBottomRadius);
		float eye_to_p_distance = xy.y * eye_to_horizon_distance;
		float center_to_p_distance = sqrt(eye_to_p_distance * eye_to_p_distance + mAtmosphere.mBottomRadius * mAtmosphere.mBottomRadius);
		r = center_to_p_distance;
		
		// X -> mu = ?
		//// Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
		//// and maximum values over all mu - obtained for (r,1) and (r,mu_horizon) -
		//// from which we can recover mu:
		float p_to_top_min_distance = mAtmosphere.mTopRadius - center_to_p_distance; // p -> top
		float p_to_top_max_distance = eye_to_p_distance + eye_to_horizon_distance; // p -> eye -> horizon(top)
		float p_to_top_distance = p_to_top_min_distance + xy.x * (p_to_top_max_distance - p_to_top_min_distance);
		mu = 1.0;
		if (p_to_top_distance != 0.0)
			mu = (eye_to_horizon_distance * eye_to_horizon_distance - eye_to_p_distance * eye_to_p_distance - p_to_top_distance * p_to_top_distance) / (2.0 * center_to_p_distance * p_to_top_distance);
		mu = clamp(mu, -1.0, 1.0); // clamp cosine
	}

	return float2(mu, r);
}

float2 Encode2D_Transmittance(float2 mu_r)
{
	if (mAtmosphere.mUnifyXYEncode)
		return Encode2D(mu_r);

	float mu = mu_r.x;
	float r = mu_r.y;

	float H = sqrt(mAtmosphere.mTopRadius * mAtmosphere.mTopRadius - mAtmosphere.mBottomRadius * mAtmosphere.mBottomRadius);
	float rho = sqrt(max(0, r * r - mAtmosphere.mBottomRadius * mAtmosphere.mBottomRadius));
	float d = DistanceToTopAtmosphereBoundary(r, mu);
	float d_min = mAtmosphere.mTopRadius - r;
	float d_max = rho + H;
	float x_mu = (d - d_min) / (d_max - d_min);
	float x_r = rho / H;

	return float2(x_mu, x_r);
}

float2 Decode2D_Irradiance(float2 xy)
{
	return Decode2D(xy);
}

float2 Encode2D_Irradiance(float2 mu_r)
{
	return Encode2D(mu_r);
}

float4 Decode4D_Scattering(float3 xyz, out bool ray_r_mu_intersects_ground)
{
	// dummy

	ray_r_mu_intersects_ground = true;

	return 0;
}

float3 Encode4D_Scattering(float4 r_mu_mu_s_nu)
{
	// dummy

	return 0;
}

float IntegrateExtinctionCoefficient(DensityProfile inProfile, float2 mu_r)
{
	const int SAMPLE_COUNT = 500;

	float mu = mu_r.x;
	float r = mu_r.y;

	float step_distance = DistanceToTopAtmosphereBoundary(r, mu) / float(SAMPLE_COUNT);
	float result = 0.0;
	for (int i = 0; i <= SAMPLE_COUNT; ++i) 
	{
		float d_i = float(i) * step_distance;

		// Distance between the current sample point and the planet center.
		float r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);

		// Extinction Coefficient (Density) based on altitude.
		float altitude = r_i - mAtmosphere.mBottomRadius;
		float beta_i = GetProfileDensity(inProfile, altitude);

		// Sample weight (from the trapezoidal rule).
		float weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;

		result += beta_i * weight_i * step_distance;
	}

	return result;
}

float3 ComputeTransmittance(float2 mu_r)
{
	// See [BN08] 2.2 (5)

	float3 ray_leigh = mAtmosphere.mRayleighScattering.xyz * IntegrateExtinctionCoefficient(mAtmosphere.mRayleighDensity, mu_r);
	float3 mie = mAtmosphere.mMieExtinction.xyz* IntegrateExtinctionCoefficient(mAtmosphere.mMieDensity, mu_r); // [TODO] Why no mie scattering ??? included ?
	float3 absorption = mAtmosphere.mAbsorptionExtinction.xyz* IntegrateExtinctionCoefficient(mAtmosphere.mAbsorptionDensity, mu_r);
	float3 transmittance = exp(-(ray_leigh + mie + absorption));

	return transmittance;
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeTransmittanceCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// XY
	float2 xy = DispatchThreadID_to_XY(inDispatchThreadID.xy, TransmittanceUAV);

	// Decode
	float2 mu_r = Decode2D_Transmittance(xy);

	// Transmittance
	float3 transmittance = ComputeTransmittance(mu_r);

	// Output
	TransmittanceUAV[inDispatchThreadID.xy] = float4(transmittance, 1.0);

	// Debug
	// float3 debug = float3(uv, 0);
	// debug = (r - 6360) / 60.0;
	// debug = mu.xxx;
	// debug = IntegrateExtinctionCoefficient(mAtmosphere.mRayleighDensity, r, mu).xxx;
	// TransmittanceUAV[inDispatchThreadID.xy] = float4(debug, 1.0);
	// TransmittanceUAV[inDispatchThreadID.xy] = float4(xy, 0, 1);
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeDirectIrradianceCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// xy
	float2 xy = DispatchThreadID_to_XY(inDispatchThreadID.xy, IrradianceUAV);

	// Decode
	float2 mu_r = Decode2D_Irradiance(xy);
	float mu_s = mu_r.x;
	float r = mu_r.y;

	// Approximate average of the cosine factor mu_s over the visible fraction of the Sun disc.
	float alpha_s = mAtmosphere.mSunAngularRadius;
	float average_cosine_factor = 0.0;
	if (mu_s < -alpha_s)
		average_cosine_factor = 0.0;
	else if (mu_s > alpha_s)
		average_cosine_factor = mu_s;
	else // [-alpha_s, alpha_s]
		average_cosine_factor = (mu_s + alpha_s) * (mu_s + alpha_s) / (4.0 * alpha_s);

	// Direct Irradiance
	float2 transmittance_uv = XY_to_UV(Encode2D_Transmittance(mu_r), TransmittanceSRV);
	float3 transmittance = TransmittanceSRV.SampleLevel(BilinearSampler, transmittance_uv, 0).xyz;
	// transmittance = ComputeTransmittance(mu_r); // compute transmittance directly
	float3 direct_irradiance = mAtmosphere.mSolarIrradiance * transmittance * average_cosine_factor;

	// Output
	DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(direct_irradiance, 1.0);
	IrradianceUAV[inDispatchThreadID.xy] = float4(0,0,0,1.0);

	// Debug
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(xy, 0, 1);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(mu_r, 0, 1);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(Encode2D_Transmittance(mu_r), 0, 1);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(transmittance, 1);
	// IrradianceUAV[inDispatchThreadID.xy] = float4(uv, 0, 1);
}

void ComputeSingleScattering(float4 r_mu_mu_s_nu, bool ray_r_mu_intersects_ground, out float3 rayleigh, out float3 mie)
{
	rayleigh = 0;
	mie = 0;

	//assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
	//assert(mu >= -1.0 && mu <= 1.0);
	//assert(mu_s >= -1.0 && mu_s <= 1.0);
	//assert(nu >= -1.0 && nu <= 1.0);

	//// Number of intervals for the numerical integration.
	//const int SAMPLE_COUNT = 50;
	//// The integration step, i.e. the length of each integration interval.
	//Length dx =
	//	DistanceToNearestAtmosphereBoundary(atmosphere, r, mu,
	//		ray_r_mu_intersects_ground) / Number(SAMPLE_COUNT);
	//// Integration loop.
	//DimensionlessSpectrum rayleigh_sum = DimensionlessSpectrum(0.0);
	//DimensionlessSpectrum mie_sum = DimensionlessSpectrum(0.0);
	//for (int i = 0; i <= SAMPLE_COUNT; ++i) {
	//	Length d_i = Number(i) * dx;
	//	// The Rayleigh and Mie single scattering at the current sample point.
	//	DimensionlessSpectrum rayleigh_i;
	//	DimensionlessSpectrum mie_i;
	//	ComputeSingleScatteringIntegrand(atmosphere, transmittance_texture,
	//		r, mu, mu_s, nu, d_i, ray_r_mu_intersects_ground, rayleigh_i, mie_i);
	//	// Sample weight (from the trapezoidal rule).
	//	Number weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
	//	rayleigh_sum += rayleigh_i * weight_i;
	//	mie_sum += mie_i * weight_i;
	//}
	//rayleigh = rayleigh_sum * dx * atmosphere.solar_irradiance *
	//	atmosphere.rayleigh_scattering;
	//mie = mie_sum * dx * atmosphere.solar_irradiance * atmosphere.mie_scattering;
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeSingleScatteringCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// xyz
	float3 xyz = DispatchThreadID_to_XYZ(inDispatchThreadID.xyz, ScatteringUAV);

	// Decode
	bool ray_r_mu_intersects_ground = false;
	float4 r_mu_mu_s_nu = Decode4D_Scattering(xyz, ray_r_mu_intersects_ground);

	// Compute
	float3 delta_rayleigh = 0;
	float3 delta_mie = 0;
	ComputeSingleScattering(r_mu_mu_s_nu, ray_r_mu_intersects_ground, delta_rayleigh, delta_mie);

	// Output
	DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(delta_rayleigh, 1);
	DeltaMieScatteringUAV[inDispatchThreadID.xyz] = float4(delta_mie, 1);
	ScatteringUAV[inDispatchThreadID.xyz] = float4(delta_rayleigh.xyz, delta_mie.x);

	// Debug
	DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.x);
	DeltaMieScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.y);
	ScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.z);
}