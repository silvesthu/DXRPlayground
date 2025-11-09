#include "Shared.h"
#include "Binding.h"

[RootSignature(ROOT_SIGNATURE_DIFF)]
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

[RootSignature(ROOT_SIGNATURE_DIFF)]
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