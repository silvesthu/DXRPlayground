#include "Constant.h"
#define CONSTANT_DEFAULT(x)
#include "Shared.inl"
#include "Binding.h"

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
	uint2 coords = inDispatchThreadID.xy;
	coords.x += inDispatchThreadID.z * 32;

	float4 input = InputUAV[coords.xy];
	OutputUAV[inDispatchThreadID.xyz] = pow(input, 1);
}
