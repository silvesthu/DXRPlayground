#pragma once
#include "Shared.h"

// #define USE_DYNAMIC_RESOURCE_CBV		// About 2x slower
#define USE_DYNAMIC_RESOURCE_SRV_UAV	// [TODO] Always enabled, need alternative implementation for comparison
#define USE_DYNAMIC_RESOURCE_SAMPLER	// [TODO] Always enabled, need alternative implementation for comparison

// CBV
#ifdef USE_DYNAMIC_RESOURCE_CBV
// 0.11ms
// Top SOLs    SM 59.4% | TEX 45.3% | L2 17.9% | VRAM 1.5% | VPC 0.0%
static ConstantBuffer<Constants> mConstants = ResourceDescriptorHeap[0];
ConstantBuffer<Constants> mConstantsUnused : register(b0, space0);
#else
// 0.05ms
// Top SOLs    SM 48.8% | TEX 17.5% | L2 0.9% | VRAM 0.4% | VPC 0.0%
ConstantBuffer<Constants> mConstants : register(b0, space0);
#endif // USE_DYNAMIC_RESOURCE_CBV

#define ROOT_SIGNATURE_BASE \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), CBV(b0, space = 0)" // RootParameterIndex::Constants

#define ROOT_SIGNATURE_COMMON \
ROOT_SIGNATURE_BASE \
ROOT_SIGNATURE_NVAPI

cbuffer RootConstantsPrepareLights : register(b0, space1)
{
	uint mNumTasks;
};
#define ROOT_SIGNATURE_PREPARE_LIGHTS \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), CBV(b0, space = 0), RootConstants(num32BitConstants=1, b0, space = 1)"

cbuffer RootConstantsDiff : register(b0, space1)
{
	uint mComputedIndex;
	uint mExpectedIndex;
	uint mOutputIndex;
};
#define ROOT_SIGNATURE_DIFF \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), CBV(b0, space = 0), RootConstants(num32BitConstants=3, b0, space = 1)"

cbuffer RootConstantsAtmosphere : register(b0, space1)
{
	uint mScatteringOrder;
}
#define ROOT_SIGNATURE_ATMOSPHERE \
"RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED), CBV(b0, space = 0), RootConstants(num32BitConstants=1, b0, space = 1)"

// Local Root Parameters, see also ShaderTableEntry, gCreateLocalRootSignature
ConstantBuffer<LocalConstants> mLocalConstants : register(b0, space100);
ConstantBuffer<Constants> mLocalCBV : register(b1, space100);
Texture2D LocalSRVs[] : register(s0, space100);

// CBV Helper
float3 GetSunDirection() { return mConstants.mSunDirection.xyz; }

// SRV Helper
static RaytracingAccelerationStructure RaytracingScene = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::RaytraceTLASSRV];

static StructuredBuffer<InstanceData> InstanceDatas = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::RaytraceInstanceDataSRV];
static StructuredBuffer<uint> Indices = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::RaytraceIndicesSRV];
static StructuredBuffer<float3> Vertices = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::RaytraceVerticesSRV];
static StructuredBuffer<float3> Normals = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::RaytraceNormalsSRV];
static StructuredBuffer<float2> UVs = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::RaytraceUVsSRV];
static StructuredBuffer<Light> Lights = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::RaytraceLightsSRV];

static StructuredBuffer<PrepareLightsTask> TaskBufferSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::TaskBufferSRV];
static StructuredBuffer<RAB_LightInfo> LightDataBufferSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::LightDataBufferSRV];
static RWStructuredBuffer<RAB_LightInfo> LightDataBufferUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::LightDataBufferUAV];

static RWStructuredBuffer<PixelInspection> PixelInspectionUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::PixelInspectionUAV];
static RWStructuredBuffer<RayInspection> RayInspectionUAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::RayInspectionUAV];

static Texture2D<float4> UVCheckerMap = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::UVCheckerSRV];
static Texture2D<float4> IESSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::IESSRV];

static Texture2D<float4> TransmittanceSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17TransmittanceSRV];
static Texture2D<float4> DeltaIrradianceSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17DeltaIrradianceSRV];
static Texture2D<float4> IrradianceSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17IrradianceSRV];
static Texture3D<float4> DeltaRayleighScatteringSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17DeltaRayleighScatteringSRV];
static Texture3D<float4> DeltaMieScatteringSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17DeltaMieScatteringSRV];
static Texture3D<float4> ScatteringSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17ScatteringSRV];
static Texture3D<float4> DeltaScatteringDensitySRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17DeltaScatteringDensitySRV];
static Texture2D<float4> TransmittanceTexSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Hillaire20TransmittanceTexSRV];
static Texture2D<float4> MultiScattTexSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Hillaire20MultiScattSRV];
static Texture2D<float4> SkyViewLutTexSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Hillaire20SkyViewLutSRV];
static Texture3D<float4> AtmosphereCameraScatteringVolumeSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Hillaire20AtmosphereCameraScatteringVolumeSRV];
static Texture2D<float4> Wilkie21SkyViewLutTexSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Wilkie21SkyViewSRV];

static Texture3D<float4> CloudShapeNoiseSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::CloudShapeNoise3DSRV];
static Texture3D<float4> CloudErosionNoiseSRV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::CloudErosionNoise3DSRV];

// UAV Helper
static RWTexture2D<float4> TransmittanceUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17TransmittanceUAV];
static RWTexture2D<float4> DeltaIrradianceUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17DeltaIrradianceUAV];
static RWTexture2D<float4> IrradianceUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17IrradianceUAV];
static RWTexture3D<float4> DeltaRayleighScatteringUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17DeltaRayleighScatteringUAV];
static RWTexture3D<float4> DeltaMieScatteringUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17DeltaMieScatteringUAV];
static RWTexture3D<float4> ScatteringUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17ScatteringUAV];
static RWTexture3D<float4> DeltaScatteringDensityUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Bruneton17DeltaScatteringDensityUAV];
static RWTexture2D<float4> TransmittanceTexUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Hillaire20TransmittanceTexUAV];
static RWTexture2D<float4> MultiScattTexUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Hillaire20MultiScattUAV];
static RWTexture2D<float4> SkyViewLutTexUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Hillaire20SkyViewLutUAV];
static RWTexture3D<float4> AtmosphereCameraScatteringVolumeUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Hillaire20AtmosphereCameraScatteringVolumeUAV];
static RWTexture2D<float4> Wilkie21SkyViewLutTexUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::Wilkie21SkyViewUAV];
static RWTexture2D<float4> ScreenColorUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::ScreenColorUAV];
static RWTexture2D<float4> ScreenDebugUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::ScreenDebugUAV];
static RWTexture2D<float4> ScreenReservoirUAV = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::ScreenReservoirUAV];

// Samplers Helper
static SamplerState BilinearClampSampler = SamplerDescriptorHeap[(uint)SamplerDescriptorIndex::BilinearClamp];
static SamplerState BilinearWrapSampler = SamplerDescriptorHeap[(uint)SamplerDescriptorIndex::BilinearWrap];
static SamplerState PointClampSampler = SamplerDescriptorHeap[(uint)SamplerDescriptorIndex::PointClamp];
static SamplerState PointWrapSampler = SamplerDescriptorHeap[(uint)SamplerDescriptorIndex::PointWrap];
