
#include "Shared.h"
#include "Binding.h"
#include "Common.h"
#include "BSDF.h"
#include "Light.h"
#include "Reservoir.h"

[RootSignature(ROOT_SIGNATURE_PREPARE_LIGHTS)]
[numthreads(64, 1, 1)]
void PrepareLightsCS(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
	EncodedTriangleLights[mTriangleLightsOffset + inDispatchThreadID.x] = (EncodedTriangleLight)0;
}