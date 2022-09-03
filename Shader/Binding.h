#pragma once
#include "Shared.inl"

// #define USE_DYNAMIC_RESOURCE_CBV		// Dynamic Resource costs +50%
// #define USE_DYNAMIC_RESOURCE_SAMPLER	// No obvious difference in DXIL

// Common
#ifdef USE_DYNAMIC_RESOURCE_SAMPLER
#define ROOT_SIGNATURE_SAMPLER
#else
#define ROOT_SIGNATURE_SAMPLER \
", StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)"	\
", StaticSampler(s1, filter = FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP)"
#endif // USE_DYNAMIC_RESOURCE_SAMPLER

// CBV
#ifdef USE_DYNAMIC_RESOURCE_CBV
static ConstantBuffer<Constants> mConstants = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Constants];
ConstantBuffer<Constants> mConstantsUnused : register(b0, space0);
#else
ConstantBuffer<Constants> mConstants : register(b0, space0);
#endif // USE_DYNAMIC_RESOURCE_CBV
#define ROOT_SIGNATURE_COMMON \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), CBV(b0, space = 0)" \
ROOT_SIGNATURE_SAMPLER

cbuffer RootConstantsDiff : register(b0, space1)
{
	uint mComputedIndex;
	uint mExpectedIndex;
	uint mOutputIndex;
};
#define ROOT_SIGNATURE_DIFF \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), CBV(b0, space = 0), RootConstants(num32BitConstants=3, b0, space = 1)" \
ROOT_SIGNATURE_SAMPLER

cbuffer RootConstantsAtmosphere : register(b0, space2)
{
	uint mScatteringOrder;
}
#define ROOT_SIGNATURE_ATMOSPHERE \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), CBV(b0, space = 0), RootConstants(num32BitConstants=1, b0, space = 2)" \
ROOT_SIGNATURE_SAMPLER

// CBV Helper
float3 GetSunDirection() { return mConstants.mSunDirection.xyz; }

// SRV Helper
static Texture2D<float4> TransmittanceSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17TransmittanceSRV];
static Texture2D<float4> DeltaIrradianceSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17DeltaIrradianceSRV];
static Texture2D<float4> IrradianceSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17IrradianceSRV];
static Texture3D<float4> DeltaRayleighScatteringSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17DeltaRayleighScatteringSRV];
static Texture3D<float4> DeltaMieScatteringSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17DeltaMieScatteringSRV];
static Texture3D<float4> ScatteringSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17ScatteringSRV];
static Texture3D<float4> DeltaScatteringDensitySRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17DeltaScatteringDensitySRV];
static Texture2D<float4> TransmittanceTexSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Hillaire20TransmittanceTexSRV];
static Texture2D<float4> MultiScattTexSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Hillaire20MultiScattSRV];
static Texture2D<float4> SkyViewLutTexSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Hillaire20SkyViewLutSRV];
static Texture3D<float4> AtmosphereCameraScatteringVolumeSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Hillaire20AtmosphereCameraScatteringVolumeSRV];
static Texture2D<float4> Wilkie21SkyViewLutTexSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Wilkie21SkyViewSRV];

static Texture3D<float4> CloudShapeNoiseSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::CloudShapeNoise3DSRV];
static Texture3D<float4> CloudErosionNoiseSRV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::CloudErosionNoise3DSRV];

// UAV Helper
static RWTexture2D<float4> TransmittanceUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17TransmittanceUAV];
static RWTexture2D<float4> DeltaIrradianceUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17DeltaIrradianceUAV];
static RWTexture2D<float4> IrradianceUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17IrradianceUAV];
static RWTexture3D<float4> DeltaRayleighScatteringUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17DeltaRayleighScatteringUAV];
static RWTexture3D<float4> DeltaMieScatteringUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17DeltaMieScatteringUAV];
static RWTexture3D<float4> ScatteringUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17ScatteringUAV];
static RWTexture3D<float4> DeltaScatteringDensityUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Bruneton17DeltaScatteringDensityUAV];
static RWTexture2D<float4> TransmittanceTexUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Hillaire20TransmittanceTexUAV];
static RWTexture2D<float4> MultiScattTexUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Hillaire20MultiScattUAV];
static RWTexture2D<float4> SkyViewLutTexUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Hillaire20SkyViewLutUAV];
static RWTexture3D<float4> AtmosphereCameraScatteringVolumeUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Hillaire20AtmosphereCameraScatteringVolumeUAV];
static RWTexture2D<float4> Wilkie21SkyViewLutTexUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::Wilkie21SkyViewUAV];
static RWTexture2D<float4> RaytracingOutput = ResourceDescriptorHeap[(int)ViewDescriptorIndex::ScreenColorUAV];

// Samplers Helper
#ifdef USE_DYNAMIC_RESOURCE_SAMPLER
static SamplerState BilinearClampSampler = SamplerDescriptorHeap[(int)SamplerDescriptorIndex::BilinearClamp];
static SamplerState BilinearWrapSampler = SamplerDescriptorHeap[(int)SamplerDescriptorIndex::BilinearWrap];
#else
SamplerState BilinearClampSampler : register(s0);
SamplerState BilinearWrapSampler : register(s1);
#endif // USE_DYNAMIC_RESOURCE_SAMPLER