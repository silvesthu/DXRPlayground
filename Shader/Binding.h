#pragma once
#include "Shared.h"

#define USE_DYNAMIC_RESOURCE_CBV		0 // About 2x slower
#define USE_DYNAMIC_RESOURCE_SRV_UAV	1 // [TODO] Always enabled, need alternative implementation for comparison
#define USE_DYNAMIC_RESOURCE_SAMPLER	1 // [TODO] Always enabled, need alternative implementation for comparison

// RootConstants
ConstantBuffer<RootConstants> mRootConstants : REGISTER_CBV(ROOT_CONSTANTS_REGISTER, COMMON_ROOT_SIGNATURE_REGISTER_SPACE);

// CBV
#if USE_DYNAMIC_RESOURCE_CBV
// 0.11ms
// Top SOLs    SM 59.4% | TEX 45.3% | L2 17.9% | VRAM 1.5% | VPC 0.0%
static ConstantBuffer<Constants> mConstants = ResourceDescriptorHeap[0];
ConstantBuffer<Constants> mConstantsUnused : REGISTER_CBV(COMMON_ROOT_CBV_REGISTER, COMMON_ROOT_SIGNATURE_REGISTER_SPACE);
#else
// 0.05ms
// Top SOLs    SM 48.8% | TEX 17.5% | L2 0.9% | VRAM 0.4% | VPC 0.0%
ConstantBuffer<Constants> mConstants : REGISTER_CBV(ROOT_CBV_REGISTER, COMMON_ROOT_SIGNATURE_REGISTER_SPACE);
#endif // USE_DYNAMIC_RESOURCE_CBV

// Local Root Parameters, see ShaderTableEntry
ConstantBuffer<LocalConstants> mLocalConstants : REGISTER_CBV(ROOT_CONSTANTS_REGISTER, LOCAL_ROOT_SIGNATURE_REGISTER_SPACE);

// ResourceDescriptorHeap Helper
#ifdef __SLANG__
#define USING_RESOURCE(type, name) type.Handle name = type.Handle(uint2((uint)ViewDescriptorIndex::name, 0));
#else
#define USING_RESOURCE(type, name) type name = ResourceDescriptorHeap[(uint)ViewDescriptorIndex::name];
#endif // __SLANG__

// SamplerDescriptorHeap Helper
#ifdef __SLANG__
#define USING_SAMPLER(type, name) type.Handle name = type.Handle(uint2((uint)SamplerDescriptorIndex::name, 0));
#else
#define USING_SAMPLER(type, name) type name = SamplerDescriptorHeap[(uint)SamplerDescriptorIndex::name];
#endif // __SLANG__
