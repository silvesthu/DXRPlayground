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

// [Bruneton08] LUT Encoding
// [Yusov13] SafetyHeightMargin, SunZenithAngle Encoding

cbuffer AtmosphereBuffer : register(b0, space2)
{
	Atmosphere mAtmosphere;
}
RWTexture2D<float4> TransmittanceUAV : register(u0, space2);
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

	// [Bruneton08 Impl] R_t * R_t - r * r == H * H - rho * rho
	// float mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
}

// r, mu -> d
float DistanceToBottomAtmosphereBoundary(float r, float mu)
{
	float R_g = mAtmosphere.mBottomRadius;

	float discriminant = r * r * (mu * mu - 1.0) + R_g * R_g;
	return ClampDistance(-r * mu - SafeSqrt(discriminant));
}

float DistanceToNearestAtmosphereBoundary(float r, float mu, bool intersects_ground)
{
	if (intersects_ground) 
		return DistanceToBottomAtmosphereBoundary(r, mu);
	else
		return DistanceToTopAtmosphereBoundary(r, mu);
}

float2 DispatchThreadID_to_XY(uint2 inDispatchThreadID, RWTexture2D<float4> inTexture)
{
	uint2 size;
	inTexture.GetDimensions(size.x, size.y);
	return inDispatchThreadID.xy / (size - 1.0);
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

// [Bruneton08] 4. Precomputations.Parameterization
float Encode_R(float r, bool trivial_encoding)
{
	float R_g = mAtmosphere.mBottomRadius; // Ground
	float R_t = mAtmosphere.mTopRadius; // Top

	if (trivial_encoding)
		return invlerp(R_g, R_t, r);
	
	float H = sqrt(R_t * R_t - R_g * R_g); // Ground to Horizon
	float rho = sqrt(max(0, r * r - R_g * R_g)); // Ground towards Horizon [0, H]
	return rho / H;
}

// Inverse of Encode_R
float Decode_R(float u_r, bool trivial_encoding)
{
	float R_g = mAtmosphere.mBottomRadius;
	float R_t = mAtmosphere.mTopRadius;

	if (trivial_encoding)
		return lerp(R_g, R_t, u_r);

	float H = sqrt(R_t * R_t - R_g * R_g);
	float rho = H * u_r;
	return sqrt(rho * rho + R_g * R_g);
}

// [Bruneton08] 4. Precomputations.Parameterization ?
float Encode_Mu(float mu, float r, bool trivial_encoding)
{
	float R_g = mAtmosphere.mBottomRadius;
	float R_t = mAtmosphere.mTopRadius;

	if (trivial_encoding)
		return invlerp(-1, 1, mu);

	float H = sqrt(R_t * R_t - R_g * R_g);					// Ground to Horizon
	float rho = sqrt(max(0, r * r - R_g * R_g));			// Ground towards Horizon [0, H]

	float d = DistanceToTopAtmosphereBoundary(r, mu);		// Distance to AtmosphereBoundary

	float d_min = R_t - r;									// P to Top = Shortest possible distance to AtmosphereBoundary
	float d_max = rho + H;									// Projected-P to Ground towards Horizon = Longest possible distance to AtmosphereBoundary

	// i.e. Encode View Zenith as P to Top distance

	float u_mu = (d - d_min) / (d_max - d_min);				// d to [0, 1]
	return u_mu;
}

// Inverse of Encode_Mu
float Decode_Mu(float u_mu, float u_r, bool trivial_encoding)
{
	float R_g = mAtmosphere.mBottomRadius;
	float R_t = mAtmosphere.mTopRadius;

	if (trivial_encoding)
		return lerp(-1, 1, u_mu);

	float r = Decode_R(u_r, trivial_encoding);

	float H = sqrt(R_t * R_t - R_g * R_g);
	float rho = H * u_r;

	float d_min = R_t - r;
	float d_max = rho + H;

	float d = d_min + u_mu * (d_max - d_min);

	float mu = InvDistanceToTopAtmosphereBoundary(r, d);

	return ClampCosine(mu);
}

float2 Decode2D(float2 u_mu_u_r, bool trivial_encoding)
{
	float mu = Decode_Mu(u_mu_u_r.x, u_mu_u_r.y, trivial_encoding);
	float r = Decode_R(u_mu_u_r.y, trivial_encoding);	

	return float2(mu, r);
}

// Inverse of Decode2D
float2 Encode2D(float2 mu_r, bool trivial_encoding)
{
	float u_mu = Encode_Mu(mu_r.x, mu_r.y, trivial_encoding);
	float u_r = Encode_R(mu_r.y, trivial_encoding);

	return float2(u_mu, u_r);
}

float4 Decode4D_Scattering(uint3 inTexCoords, out bool intersects_ground)
{
	uint3 size;
	ScatteringUAV.GetDimensions(size.x, size.y, size.z);

	uint x_bin_count = mAtmosphere.mXBinCount;
	uint x_bin_size = size.x / x_bin_count;

	float4 xyzw = 0;
	xyzw.x = (inTexCoords.x % x_bin_size) / (x_bin_size - 1.0);
	xyzw.y = (inTexCoords.y) / (size.y - 1.0);
	xyzw.z = (inTexCoords.z) / (size.z - 1.0);
	xyzw.w = (inTexCoords.x / x_bin_size) / (x_bin_count - 1.0);

	// [0, 1]^4
	float u_r = xyzw.z;
	float u_mu = xyzw.y;
	float u_mu_s = xyzw.x;
	float u_nu = xyzw.w;

	float r;	// Height
	float mu;	// Cosine of view zenith
	float mu_s; // Cosine of sun zenith
	float nu;	// Cosine of view sun angle

	if (mAtmosphere.mTrivialAxisEncoding)
	{
		r = lerp(mAtmosphere.mBottomRadius, mAtmosphere.mTopRadius, u_r);
		mu = u_mu * 2.0 - 1.0;
		mu_s = u_mu_s * 2.0 - 1.0;
		nu = u_nu * 2.0 - 1.0;

		intersects_ground = mu < 0 && (r * mu < mAtmosphere.mBottomRadius);

		return float4(r, mu, mu_s, nu);
	}

	// [Bruneton08] Parameterization
	float R_t = mAtmosphere.mTopRadius; // Top
	float R_g = mAtmosphere.mBottomRadius; // Ground
	float H = sqrt(R_t * R_t - R_g * R_g); // Ground to Horizon
	float rho = H * u_r; // Ground towards Horizon [0, H]
	
	// r - Height
	{
		// Non-linear mapping, Figure 3
		r = sqrt(rho * rho + R_g * R_g);

		// Same as
		// r = Decode_R(u_r, false);
	}

	// mu - View Zenith
	{
		// [TODO] Result is different from [Bruneton08 Impl], discontinuity handling? Check last slice, u_r = 1

		float rho = H * u_r;
		if (inTexCoords.y <= size.y / 2)
		{
			// View Zenith -> P to Ground distance

			intersects_ground = true;

			// [0.0 ~ 0.5] -> [1.0 -> 0.0]
			float y = (size.y / 2 - inTexCoords.y) * 1.0 / (size.y / 2 - 1); 

			float d_min = r - R_g;
			float d_max = rho;
			float d = d_min + (d_max - d_min) * y; // Distance to Ground

			mu = d == 0.0 ? float(-1.0) : ClampCosine((- rho * rho - d * d) / (2.0 * r * d));
		}
		else
		{
			// Same as
			// mu = Decode_Mu(y, u_r, false);
			// View Zenith -> P to Top distance

			intersects_ground = false;

			// [0.5 ~ 1.0] -> [0.0 -> 1.0]
			float y = (inTexCoords.y - size.y / 2) * 1.0 / (size.y / 2 - 1);

			float d_min = R_t - r;
			float d_max = rho + H;
			float d = d_min + (d_max - d_min) * y; // Distance to AtmosphereBoundary

			mu = d == 0.0 ? float(1.0) : ClampCosine((H * H - rho * rho - d * d) / (2.0 * r * d));
		}
	}

	// mu_s - Sun Zenith
	{
		// [NOTE] Fitted curve is most likely to be created with 102.0
		bool use_half_precision = true;
		float max_sun_zenith_angle = (use_half_precision ? 102.0 : 120.0) / 180.0 * MATH_PI;
		float mu_s_min = cos(max_sun_zenith_angle);

		float d_min = R_t - R_g;
		float d_max = H;

		float D = DistanceToTopAtmosphereBoundary(R_g, mu_s_min);

		float A = (D - d_min) / (d_max - d_min);
		float a = (A - u_mu_s * A) / (1.0 + u_mu_s * A);

		float d = d_min + min(a, A) * (d_max - d_min);

		mu_s = d == 0.0 ? float(1.0) : ClampCosine((H * H - d * d) / (2.0 * R_g * d));

		// [Bruneton08] Fitted curve ?
		// mu_s = (log(1.0 - (1 - exp(-3.6)) * u_mu_s) + 0.6) / -3.0;

		// [Elek09] Fitted curve ?
		// mu_s = (log(1.0 - (1 - exp(-3.6)) * u_mu_s) + 0.8) / -2.8;

		// [Yusov13] Better fitted curve ?
		// mu_s = tan((2.0 * u_mu_s - 1.0 + 0.26) * 1.1) / tan(1.26 * 1.1);
	}

	// nu - View Sun Angle
	{
		nu = ClampCosine(u_nu * 2.0 - 1.0);
	}

	// ?
	nu = clamp(nu, mu * mu_s - sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s)), mu * mu_s + sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s)));

	return float4(r, mu, mu_s, nu);
}

float3 Encode4D_Scattering(float4 r_mu_mu_s_nu)
{
	// [TODO]
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
	// See [Bruneton08] 2.2 (5)

	float3 rayleigh = mAtmosphere.mRayleighExtinction.xyz * IntegrateExtinctionCoefficient(mAtmosphere.mRayleighDensity, mu_r);
	float3 mie = mAtmosphere.mMieExtinction.xyz* IntegrateExtinctionCoefficient(mAtmosphere.mMieDensity, mu_r);
	float3 ozone = mAtmosphere.mOzoneExtinction.xyz* IntegrateExtinctionCoefficient(mAtmosphere.mOzoneDensity, mu_r);

	float3 transmittance = exp(-(rayleigh + mie + ozone));
	return transmittance;
}

float3 GetTransmittanceToTopAtmosphereBoundary(float r, float mu)
{
	float2 mu_r = float2(mu, r);

	float2 transmittance_uv = XY_to_UV(Encode2D(mu_r, mAtmosphere.mTrivialAxisEncoding), TransmittanceSRV);
	float3 transmittance = TransmittanceSRV.SampleLevel(BilinearSampler, transmittance_uv, 0).xyz;
	// transmittance = ComputeTransmittance(mu_r); // compute transmittance directly

	return transmittance;
}

float3 GetTransmittance(float r, float mu, float d, bool ray_r_mu_intersects_ground) 
{
	float r_d = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));
	float mu_d = ClampCosine((r * mu + d) / r_d);

	if (ray_r_mu_intersects_ground)
		return min(GetTransmittanceToTopAtmosphereBoundary(r_d, -mu_d) / GetTransmittanceToTopAtmosphereBoundary(r, -mu), 1.0);
	else
		return min(GetTransmittanceToTopAtmosphereBoundary(r, mu) / GetTransmittanceToTopAtmosphereBoundary(r_d, mu_d), 1.0);
}

float3 GetTransmittanceToSun(float r, float mu_s) 
{
	float sin_theta_h = mAtmosphere.mBottomRadius / r;
	float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));

	return GetTransmittanceToTopAtmosphereBoundary(r, mu_s) * 
		smoothstep(-sin_theta_h * mAtmosphere.mSunAngularRadius, sin_theta_h * mAtmosphere.mSunAngularRadius, mu_s - cos_theta_h);
}

void ComputeSingleScatteringIntegrand(float4 r_mu_mu_s_nu, float d, bool ray_r_mu_intersects_ground, out float3 rayleigh, out float3 mie) 
{
	float r = r_mu_mu_s_nu.x;
	float mu = r_mu_mu_s_nu.y;
	float mu_s = r_mu_mu_s_nu.z;
	float nu = r_mu_mu_s_nu.w;

	float r_d = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));

	float mu_s_d = ClampCosine((r * mu_s + d * nu) / r_d);

	float3 transmittance = GetTransmittance(r, mu, d, ray_r_mu_intersects_ground) * GetTransmittanceToSun(r_d, mu_s_d);

	rayleigh = transmittance * GetProfileDensity(mAtmosphere.mRayleighDensity, r_d - mAtmosphere.mBottomRadius);
	mie = transmittance * GetProfileDensity(mAtmosphere.mMieDensity, r_d - mAtmosphere.mBottomRadius);
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
	float2 mu_r = Decode2D(xy, mAtmosphere.mTrivialAxisEncoding);

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
	float2 mu_r = Decode2D(xy, true);
	float mu_s = mu_r.x;
	float r = mu_r.y;

	// Approximate average of the cosine factor mu_s over the visible fraction of the Sun disc.
	float alpha_s = mAtmosphere.mSunAngularRadius;
	float average_cosine_factor = 0.0;
	if (mu_s < -alpha_s) // Sun below horizon
		average_cosine_factor = 0.0;
	else if (mu_s > alpha_s) // Sun above horizon
		average_cosine_factor = mu_s;
	else // [-alpha_s, alpha_s] Sun behind horizon
		average_cosine_factor = (mu_s + alpha_s) * (mu_s + alpha_s) / (4.0 * alpha_s);

	// Direct Irradiance
	float3 transmittance = GetTransmittanceToTopAtmosphereBoundary(r, mu_s);
	float3 direct_irradiance = transmittance * mAtmosphere.mSolarIrradiance * average_cosine_factor; // [Bruneton08] 2.2 (9) L_0

	// Output
	DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(direct_irradiance, 1.0);
	IrradianceUAV[inDispatchThreadID.xy] = float4(0,0,0,1.0);

	// Debug
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(xy, 0, 1);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(mu_r, 0, 1);
	// DeltaIrradianceUAV[inDispatchThreadID.xy] = float4(transmittance, 1);
	// IrradianceUAV[inDispatchThreadID.xy] = float4(uv, 0, 1);
}

void ComputeSingleScattering(float4 r_mu_mu_s_nu, bool intersects_ground, out float3 rayleigh, out float3 mie)
{
	float r = r_mu_mu_s_nu.x;
	float mu = r_mu_mu_s_nu.y;
	float mu_s = r_mu_mu_s_nu.z;
	float nu = r_mu_mu_s_nu.w;

	// Number of intervals for the numerical integration.
	const int SAMPLE_COUNT = 50;

	// The integration step, i.e. the length of each integration interval.
	float dx = DistanceToNearestAtmosphereBoundary(r, mu, intersects_ground) / float(SAMPLE_COUNT);

	// Integration loop.
	float3 rayleigh_sum = 0.0;
	float3 mie_sum = 0.0;

	for (int i = 0; i <= SAMPLE_COUNT; ++i) 
	{
		float d_i = i * dx;

		// The Rayleigh and Mie single scattering at the current sample point.
		float3 rayleigh_i;
		float3 mie_i;

		ComputeSingleScatteringIntegrand(r_mu_mu_s_nu, d_i, intersects_ground, rayleigh_i, mie_i);

		// Sample weight (from the trapezoidal rule).
		float weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;

		rayleigh_sum += rayleigh_i * weight_i;
		mie_sum += mie_i * weight_i;
	}

	rayleigh = rayleigh_sum * dx * mAtmosphere.mSolarIrradiance* mAtmosphere.mRayleighScattering;
	mie = mie_sum * dx * mAtmosphere.mSolarIrradiance * mAtmosphere.mMieScattering;
}

[RootSignature(AtmosphereRootSignature)]
[numthreads(8, 8, 1)]
void ComputeSingleScatteringCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	// Decode
	bool intersects_ground = false;
	float4 r_mu_mu_s_nu = Decode4D_Scattering(inDispatchThreadID.xyz, intersects_ground);

	// Compute
	float3 delta_rayleigh = 0;
	float3 delta_mie = 0;
	ComputeSingleScattering(r_mu_mu_s_nu, intersects_ground, delta_rayleigh, delta_mie);

	// [TODO]
	float3x3 luminance_from_radiance = { 1,0,0,0,1,0,0,0,1 };

	// Output
	DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(delta_rayleigh, 1);
	DeltaMieScatteringUAV[inDispatchThreadID.xyz] = float4(delta_mie, 1);
	ScatteringUAV[inDispatchThreadID.xyz] = float4(mul(luminance_from_radiance, delta_rayleigh).xyz, mul(luminance_from_radiance, delta_mie).x);

	// Debug
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(ComputeTransmittance(Decode2D(float2(1,1), mAtmosphere.mTrivialAxisEncoding)), 1) * 100; 
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = TransmittanceSRV.SampleLevel(BilinearSampler, float2(1,1), 0) * 100;
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(r_mu_mu_s_nu.xyz, 1);
	// DeltaRayleighScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.x);
	// DeltaMieScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.y);
	// ScatteringUAV[inDispatchThreadID.xyz] = float4(xyz, xyz.z);
}