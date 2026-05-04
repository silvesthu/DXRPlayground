#include "Shared.h"
#include "Binding.h"

[numthreads(8, 8, 1)]
void DiffTexture2DShader(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	uint computed_index = mRootConstants.mData0.x;
	uint expected_index = mRootConstants.mData0.y;
	uint output_index = mRootConstants.mData0.z;

	RWTexture2D<float4> computed = ResourceDescriptorHeap[computed_index];
	RWTexture2D<float4> expected = ResourceDescriptorHeap[expected_index];
	RWTexture2D<float4> output = ResourceDescriptorHeap[output_index];

	bool equal = all(computed[inDispatchThreadID.xy] == expected[inDispatchThreadID.xy]);
	output[inDispatchThreadID.xy] = equal ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
}

[numthreads(8, 8, 1)]
void DiffTexture3DShader(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	uint computed_index = mRootConstants.mData0.x;
	uint expected_index = mRootConstants.mData0.y;
	uint output_index	= mRootConstants.mData0.z;

	RWTexture3D<float4> computed = ResourceDescriptorHeap[computed_index];
	RWTexture3D<float4> expected = ResourceDescriptorHeap[expected_index];
	RWTexture3D<float4> output = ResourceDescriptorHeap[output_index];

	bool equal = all(computed[inDispatchThreadID.xyz] == expected[inDispatchThreadID.xyz]);
	output[inDispatchThreadID.xyz] = equal ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
}