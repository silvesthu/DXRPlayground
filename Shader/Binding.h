#pragma once

// CBV
cbuffer PerFrameConstantsBuffer : register(b0, space0)
{
    PerFrameConstants mPerFrameConstants;
};

// CBV Helper
float3 GetSunDirection() { return mPerFrameConstants.mSunDirection.xyz; }

// SRV
RaytracingAccelerationStructure RaytracingScene : register(t0, space0);
StructuredBuffer<InstanceData> InstanceDatas : register(t1, space0);
StructuredBuffer<uint> Indices : register(t2, space0);
StructuredBuffer<float3> Vertices : register(t3, space0);
StructuredBuffer<float3> Normals : register(t4, space0);
StructuredBuffer<float2> UVs : register(t5, space0);
StructuredBuffer<Light> LightDataBuffer : register(t6, space0);

// UAV
RWTexture2D<float4> RaytracingOutput : register(u0, space0);

// Sampler
SamplerState BilinearSampler : register(s0);
SamplerState BilinearWrapSampler : register(s1);
