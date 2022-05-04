#pragma once

// CBV
cbuffer PerFrameConstantsBuffer : register(b0, space0)
{
    PerFrameConstants mPerFrameConstants;
};

// Helper
float3 GetSunDirection() { return mPerFrameConstants.mSunDirection.xyz; }

// UAV
RWTexture2D<float4> RaytracingOutput : register(u0, space0);

// Sampler
SamplerState BilinearSampler : register(s0);
SamplerState BilinearWrapSampler : register(s1);
