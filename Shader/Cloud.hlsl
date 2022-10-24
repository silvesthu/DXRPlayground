#include "Constant.h"
#include "Shared.h"
#include "Binding.h"

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void CloudShapeNoiseCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	RWTexture2D<float4> input_UAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::CloudShapeNoise2DUAV];
	RWTexture3D<float4> output_UAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::CloudShapeNoise3DUAV];
	
	uint2 coords = inDispatchThreadID.xy;
	coords.x += inDispatchThreadID.z * 128;

	float4 input = input_UAV[coords.xy];
	output_UAV[inDispatchThreadID.xyz] = input;
}

[RootSignature(ROOT_SIGNATURE_COMMON)]
[numthreads(8, 8, 1)]
void CloudErosionNoiseCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	RWTexture2D<float4> input_UAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::CloudErosionNoise2DUAV];
	RWTexture3D<float4> output_UAV = ResourceDescriptorHeap[(int)ViewDescriptorIndex::CloudErosionNoise3DUAV];
	
	uint2 coords = inDispatchThreadID.xy;
	coords.x += inDispatchThreadID.z * 32;

	float4 input = input_UAV[coords.xy];
	output_UAV[inDispatchThreadID.xyz] = input;
}
