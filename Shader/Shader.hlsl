#include "Constant.h"
#define CONSTANT_DEFAULT(x)
#include "Shared.inl"
#include "Common.inl"
#include "Util.inl"

#ifndef SHADER_PROFILE_LIB
#define ENABLE_RAY_QUERY
#endif // SHADER_PROFILE_LIB

//////////////////////////////////////////////////////////////////////////////////
// Common

RWTexture2D<float4> RaytracingOutput : register(u0, space0);
cbuffer PerFrameConstantsBuffer : register(b0, space0)
{
	PerFrameConstants mPerFrameConstants;
};

SamplerState BilinearSampler : register(s0);
SamplerState BilinearWrapSampler : register(s1);

float3 GetSunDirection() { return mPerFrameConstants.mSunDirection.xyz; }

#include "RayQuery.inl"

#include "Planet.inl"
#include "AtmosphereIntegration.inl"
#include "CloudIntegration.inl"

//////////////////////////////////////////////////////////////////////////////////

RaytracingAccelerationStructure RaytracingScene : register(t0, space0);
StructuredBuffer<InstanceData> InstanceDataBuffer : register(t1, space0);
StructuredBuffer<uint> Indices : register(t2, space0);
StructuredBuffer<float3> Vertices : register(t3, space0);
StructuredBuffer<float3> Normals : register(t4, space0);
StructuredBuffer<float2> UVs : register(t5, space0);

#define DEBUG_PIXEL_RADIUS (3)

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ToneMapping_ACES_Knarkowicz(float3 x)
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

		float exposure_normalization_factor = 1.0 / (pow(2.0, mPerFrameConstants.mEV100) * kLensSaturation); // = 1.0 / luminance_max
		normalized_luminance = luminance * (exposure_normalization_factor / kPreExposure);

		// [Reference]
		// https://en.wikipedia.org/wiki/Exposure_value
		// https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
		// https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
		// https://docs.unrealengine.com/en-US/RenderingAndGraphics/PostProcessEffects/ColorGrading/index.html
	}

	float3 tone_mapped_color = 0;
	// Tone Mapping
	{
		switch (mPerFrameConstants.mToneMappingMode)
		{
			case ToneMappingMode::Knarkowicz:	tone_mapped_color = ToneMapping_ACES_Knarkowicz(normalized_luminance); break;
            case ToneMappingMode::Passthrough:	// fallthrough
			default:							tone_mapped_color = normalized_luminance; break;
        }

		// [Reference]
		// https://github.com/ampas/aces-dev
		// https://docs.unrealengine.com/en-US/RenderingAndGraphics/PostProcessEffects/ColorGrading/index.html
	}

	return tone_mapped_color;
}

[shader("miss")]
void DefaultMiss(inout RayPayload payload)
{
	payload.mDone = true;

    float3 sky_luminance = GetSkyLuminance();

	float3 cloud_transmittance = 1;
    float3 cloud_luminance = 0;
    RaymarchCloud(cloud_transmittance, cloud_luminance);
	
    float3 emission = lerp(sky_luminance, cloud_luminance, 1.0 - cloud_transmittance);

	payload.mEmission = payload.mEmission + payload.mThroughput * emission;
}

HitInfo HitInternal(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
	HitInfo hit_info = (HitInfo)0;
	hit_info.mDone = true;

	// For more system value intrinsics, see https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html 
	float3 barycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);

	// Only support 32bit index, see https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl for reference
	uint index_count_per_triangle = 3;
	uint base_index = sGetPrimitiveIndex() * index_count_per_triangle + InstanceDataBuffer[sGetInstanceID()].mIndexOffset;
	uint3 indices = uint3(Indices[base_index], Indices[base_index + 1], Indices[base_index + 2]);

	// Attributes
	float3 vertices[3] = { Vertices[indices[0]], Vertices[indices[1]], Vertices[indices[2]] };
	float3 vertex = vertices[0] * barycentrics.x + vertices[1] * barycentrics.y + vertices[2] * barycentrics.z;

	float3 normals[3] = { Normals[indices[0]], Normals[indices[1]], Normals[indices[2]] };
	float3 normal = normalize(normals[0] * barycentrics.x + normals[1] * barycentrics.y + normals[2] * barycentrics.z);

	float2 uvs[3] = { UVs[indices[0]], UVs[indices[1]], UVs[indices[2]] };
	float2 uv = uvs[0] * barycentrics.x + uvs[1] * barycentrics.y + uvs[2] * barycentrics.z;

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
	{
        float3 raw_hit_position = sGetWorldRayOrigin() + sGetWorldRayDirection() * sGetRayTCurrent();
        hit_info.mPosition = raw_hit_position + normal * 0.001;
    }

	// Participating Media
    float3 in_scattering = 0;
    float3 transmittance = 0;
    GetSkyLuminanceToPoint(in_scattering, transmittance);
	{
        hit_info.mInScattering = in_scattering;
		hit_info.mTransmittance = transmittance;
	}

	// Reflection
	{
		// TODO: Revisit. Support tangent ?
		// onb - build_from_w
		float3 axis[3];
		axis[2] = normal;
		float3 a = (abs(axis[2].x) > 0.9) ? float3(0, 1, 0) : float3(1, 0, 0);
		axis[1] = normalize(cross(axis[2], a));
		axis[0] = cross(axis[2], axis[1]);
		
		// Based on Mitsuba2
        switch (InstanceDataBuffer[sGetInstanceID()].mMaterialType)
        {
            case MaterialType::Diffuse:
	            {
            		hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mEmission * (kEmissionBoostScale * kPreExposure);

            		// random
            		float3 direction = RandomCosineDirection(payload.mRandomState);

            		// onb - local
            		hit_info.mReflectionDirection = normalize(direction.x * axis[0] + direction.y * axis[1] + direction.z * axis[2]);

            		// pdf - exact distribution - should cancel out
            		float cosine = dot(normal, hit_info.mReflectionDirection);
            		hit_info.mScatteringPDF = cosine <= 0 ? 0 : cosine / MATH_PI;
            		hit_info.mSamplingPDF = cosine <= 0 ? 0 : cosine / MATH_PI;
            		hit_info.mAlbedo = InstanceDataBuffer[sGetInstanceID()].mAlbedo;
            		hit_info.mDone = false;
	            }
                break;
            case MaterialType::RoughConductor:
				{
                    hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mEmission * (kEmissionBoostScale * kPreExposure);

            		// TODO: Check visible normal

            		float a = InstanceDataBuffer[sGetInstanceID()].mRoughnessAlpha;
            		float a2 = a * a;

            		// Microfacet
            		float3 H; // Microfacet normal (Half-vector)
            		{                    	
						float e0 = RandomFloat01(payload.mRandomState);
                    	float e1 = RandomFloat01(payload.mRandomState);
                    	
                    	// 2D Distribution -> GGX Distribution (Polar)
                    	float cos_theta = sqrt((1.0 - e0) / ((a2 - 1) * e0 + 1.0));
                    	float sin_theta = sqrt( 1 - cos_theta * cos_theta );
                    	float phi = 2 * MATH_PI * e1;

                    	// Polar -> Cartesian
                    	H.x = sin_theta * cos(phi);
                    	H.y = sin_theta * sin(phi);
                    	H.z = cos_theta;

                    	// Tangent -> World
                    	H = normalize(H.x * axis[0] + H.y * axis[1] + H.z * axis[2]);
            		}

            		// TODO: Better to calculate in tangent space?
            		float3 V = -sGetWorldRayDirection();
            		float HoV = dot(H, V);
            		float3 L = 2.0 * HoV * H - V;
            		float NoH = dot(normal, H);
            		float NoV = dot(normal, V);
            		float HoL = dot(H, L);
            		float NoL = dot(normal, L);

            		float G = G_SmithGGX(NoL, NoV, a2);
            		float3 F = F_Schlick(InstanceDataBuffer[sGetInstanceID()].mReflectance, HoV);

            		hit_info.mReflectionDirection = L;
            		hit_info.mAlbedo = G * F / (4.0f * NoV);
            		hit_info.mScatteringPDF = 1.0;
            		hit_info.mSamplingPDF = NoH / (4.0f * abs(HoL));
            		hit_info.mDone = false;
                }
                break;
            case MaterialType::None:
			default:
                break;
        }
    }

	// Debug - Global
	{
		bool terminate = true;
		switch (mPerFrameConstants.mDebugMode)
		{
		case DebugMode::Barycentrics: 			hit_info.mEmission = barycentrics; break;
		case DebugMode::Vertex: 				hit_info.mEmission = vertex; break;
		case DebugMode::Normal: 				hit_info.mEmission = normal * 0.5 + 0.5; break;
		case DebugMode::UV:						hit_info.mEmission = float3(uv, 0.0); break;
		case DebugMode::Albedo: 				hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mAlbedo; break;
		case DebugMode::Reflectance: 			hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mReflectance; break;
		case DebugMode::Emission: 				hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mEmission; break;
		case DebugMode::RoughnessAlpha: 		hit_info.mEmission = InstanceDataBuffer[sGetInstanceID()].mRoughnessAlpha; break;
		case DebugMode::Transmittance:			hit_info.mEmission = transmittance; break;
		case DebugMode::InScattering:			hit_info.mEmission = in_scattering; break;
		case DebugMode::RecursionCount:			terminate = false; break;
		case DebugMode::RussianRouletteCount:	terminate = false; break;
		default:								terminate = false; break;
		}

		if (terminate)
		{
			hit_info.mDone = true;
			return hit_info;
		}
	}

	// Debug - Per instance
	if (mPerFrameConstants.mDebugInstanceIndex == sGetInstanceID())
	{
		switch (mPerFrameConstants.mDebugInstanceMode)
		{
		case DebugInstanceMode::Barycentrics: 	hit_info.mEmission = barycentrics; hit_info.mDone = true; return hit_info;
		case DebugInstanceMode::Mirror: 		hit_info.mAlbedo = 1; hit_info.mReflectionDirection = reflect(sGetWorldRayDirection(), normal); return hit_info;
		default:								break;
		}
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
	payload.mThroughput = hit_info.mSamplingPDF <= 0 ? 0 : payload.mThroughput * hit_info.mAlbedo * hit_info.mTransmittance * hit_info.mScatteringPDF / hit_info.mSamplingPDF;
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

	float2 crd = float2(launchIndex.xy) + 0.5;
	float2 dims = float2(launchDim.xy);

	float2 d = ((crd / dims) * 2.f - 1.f); // 0~1 => -1~1
	d.y = -d.y;
	
	RayDesc ray;
	ray.Origin = mPerFrameConstants.mCameraPosition.xyz;
	ray.Direction = normalize(mPerFrameConstants.mCameraDirection.xyz + mPerFrameConstants.mCameraRightExtend.xyz * d.x + mPerFrameConstants.mCameraUpExtend.xyz * d.y);
	ray.TMin = 0.001;				// Near
	ray.TMax = 100000;				// Far

	RayPayload payload = (RayPayload)0;
	payload.mThroughput = 1; // Camera gather all the light
	payload.mRandomState = uint(uint(sGetDispatchRaysIndex().x) * uint(1973) + uint(sGetDispatchRaysIndex().y) * uint(9277) + uint(mPerFrameConstants.mAccumulationFrameCount) * uint(26699)) | uint(1); // From https://www.shadertoy.com/view/tsBBWW

#ifdef ENABLE_RAY_QUERY
	// Note that RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH will give first hit for "Any Hit". The result may not be the closest one
	// https://docs.microsoft.com/en-us/windows/win32/direct3d12/ray_flag
	RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	uint additional_ray_flags = 0;
	uint ray_instance_mask = 0xffffffff;
#endif // ENABLE_RAY_QUERY

	uint recursion = 0;
	for (;;)
	{
#ifdef ENABLE_RAY_QUERY
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
			0,						// MultiplierForGeometryContributionToHitGroupIndex, 16bits
			0,						// MissShaderIndex
			ray,					// RayDesc
			payload					// payload_t
		);
#endif // ENABLE_RAY_QUERY

		if (payload.mDone)
			break;

		ray.Origin = payload.mPosition;
		ray.Direction = payload.mReflectionDirection;

		// Russian Roulette
		// http://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/Russian_Roulette_and_Splitting.html
		// https://computergraphics.stackexchange.com/questions/2316/is-russian-roulette-really-the-answer
		if (recursion >= mPerFrameConstants.mRecursionCountMax)
		{
			if (mPerFrameConstants.mRecursionMode == RecursionMode::RussianRoulette && recursion <= 8)
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

	// Accumulation
	{
		float3 current_output = payload.mEmission;

		float3 previous_output = RaytracingOutput[sGetDispatchRaysIndex().xy].xyz;
		previous_output = max(0, previous_output); // Eliminate nan
		float3 mixed_output = lerp(previous_output, current_output, 1.0f / (float)(mPerFrameConstants.mAccumulationFrameCount));

		if (recursion == 0)
			mixed_output = current_output;

		if (mPerFrameConstants.mDebugMode == DebugMode::RecursionCount)
			mixed_output = hsv2rgb(float3(recursion * 1.0 / (mPerFrameConstants.mRecursionCountMax + 1), 1, 1));

		if (mPerFrameConstants.mDebugMode == DebugMode::RussianRouletteCount)
			mixed_output = hsv2rgb(float3(max(0.0, (recursion * 1.0 - mPerFrameConstants.mRecursionCountMax * 1.0)) / 10.0 /* for visualization only */, 1, 1));

		// [TODO] Ray visualization ?
		// if (all(abs((int2)sGetDispatchRaysIndex().xy - (int2)mDebugCoord) < DEBUG_PIXEL_RADIUS))
		// 	mixed_output = 0;

		RaytracingOutput[sGetDispatchRaysIndex().xy] = float4(mixed_output, 1);
	}
}

[shader("raygeneration")]
void DefaultRayGeneration()
{
	TraceRay();
}

#ifdef ENABLE_RAY_QUERY
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
#endif // ENABLE_RAY_QUERY

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef SHADER_PROFILE_LIB

cbuffer DiffTextureConstants : register(b0, space11)
{
	uint mComputedIndex;
	uint mExpectedIndex;
	uint mOutputIndex;
};

[RootSignature("RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), RootConstants(num32BitConstants=3, b0, space = 11)")]
[numthreads(8, 8, 1)]
void DiffTexture2DShader(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	RWTexture2D<float4> computed = ResourceDescriptorHeap[mComputedIndex];
	RWTexture2D<float4> expected = ResourceDescriptorHeap[mExpectedIndex];
	RWTexture2D<float4> output = ResourceDescriptorHeap[mOutputIndex];

	bool equal = all(computed[inDispatchThreadID.xy] == expected[inDispatchThreadID.xy]);
	output[inDispatchThreadID.xy] = equal ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
}

[RootSignature("RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), RootConstants(num32BitConstants=3, b0, space = 11)")]
[numthreads(8, 8, 1)]
void DiffTexture3DShader(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	RWTexture3D<float4> computed = ResourceDescriptorHeap[mComputedIndex];
	RWTexture3D<float4> expected = ResourceDescriptorHeap[mExpectedIndex];
	RWTexture3D<float4> output = ResourceDescriptorHeap[mOutputIndex];

	bool equal = all(computed[inDispatchThreadID.xyz] == expected[inDispatchThreadID.xyz]);
	output[inDispatchThreadID.xyz] = equal ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
}

#endif // SHADER_PROFILE_LIB
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

	if (mPerFrameConstants.mDebugMode == DebugMode::None)
		color = LuminanceToColor(color /* as luminance */);

	color = ApplySRGBCurve(color);
	return float4(color, 1);
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
