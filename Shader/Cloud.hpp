#include "Shared.h"
#include "Binding.h"

[numthreads(8, 8, 1)]
void CloudShapeNoiseCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	USING_RESOURCE(RWTexture2D<float4>, CloudShapeNoise2DUAV);
	USING_RESOURCE(RWTexture3D<float4>, CloudShapeNoise3DUAV);
	
	uint2 coords = inDispatchThreadID.xy;
	coords.x += inDispatchThreadID.z * 128;

	float4 input = CloudShapeNoise2DUAV[coords.xy];
	CloudShapeNoise3DUAV[inDispatchThreadID.xyz] = input;
}

[numthreads(8, 8, 1)]
void CloudErosionNoiseCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	USING_RESOURCE(RWTexture2D<float4>, CloudErosionNoise2DUAV);
	USING_RESOURCE(RWTexture3D<float4>, CloudErosionNoise3DUAV);
	
	uint2 coords = inDispatchThreadID.xy;
	coords.x += inDispatchThreadID.z * 32;

	float4 input = CloudErosionNoise2DUAV[coords.xy];
	CloudErosionNoise3DUAV[inDispatchThreadID.xyz] = input;
}
