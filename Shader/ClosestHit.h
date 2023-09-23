
#include "Shared.h"
#include "Binding.h"
#include "Common.h"

void ClosestHitOverride(inout RayPayload ioPayload, in BuiltInTriangleIntersectionAttributes inAttributes)
{
	// Override all ClosestHit shaders
	// ioPayload.mData = float4(1, 0, 1, 0);

	// D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS
	// ioPayload.mData = mLocalConstants.mShaderIndex;	// LocalRootSignature

	// D3D12_ROOT_PARAMETER_TYPE_CBV
	// ioPayload.mData = abs(sin(mConstants.mTime));	// GlobalRootSignature
	// ioPayload.mData = abs(sin(mLocalCBV.mTime));		// LocalRootSignature

	// D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE
	// ioPayload.mData = UVCheckerMap.SampleLevel(BilinearClampSampler, inAttributes.barycentrics, 0);											// GlobalRootSignature
	// ioPayload.mData = LocalSRVs[(uint)ViewDescriptorIndex::UVCheckerMap].SampleLevel(BilinearClampSampler, inAttributes.barycentrics, 0);	// LocalRootSignature
}