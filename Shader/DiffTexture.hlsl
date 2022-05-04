#include "Constant.h"
#define CONSTANT_DEFAULT(x)
#include "Shared.inl"
#include "Binding.h"

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