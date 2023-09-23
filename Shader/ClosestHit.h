
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

void ClosestHitOverride(inout RayPayload ioPayload, in BuiltInTriangleIntersectionAttributes inAttributes)
{
	// Override all ClosestHit shaders
	// ioPayload.mData = float4(1, 0, 1, 0);

	// Reference local root constants
	// ioPayload.mData = mLocalConstants.mShaderIndex;

	// Reference CBV
	// ioPayload.mData = abs(sin(mConstants.mTime));	// GlobalRootSignature
	// ioPayload.mData = abs(sin(mLocalCBV.mTime));		// LocalRootSignature

	// Reference descriptor table
	// ioPayload.mData = UVCheckerMap.SampleLevel(BilinearClampSampler, inAttributes.barycentrics, 0);											// GlobalRootSignature
	// ioPayload.mData = LocalSRVs[(uint)ViewDescriptorIndex::UVCheckerMap].SampleLevel(BilinearClampSampler, inAttributes.barycentrics, 0);	// LocalRootSignature
}