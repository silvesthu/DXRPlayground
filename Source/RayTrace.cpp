#include "Common.h"

void gRaytrace(ID3D12Resource* inFrameRenderTargetResource)
{
	// Output - Copy -> UAV
	gBarrierTransition(gCommandList, gDxrOutputResource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Setup D3D12_DISPATCH_RAYS_DESC
	D3D12_DISPATCH_RAYS_DESC dispatch_rays_desc = {};
	{
		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
		gSwapChain->GetDesc1(&swap_chain_desc);

		dispatch_rays_desc.Width = swap_chain_desc.Width;
		dispatch_rays_desc.Height = swap_chain_desc.Height;
		dispatch_rays_desc.Depth = 1;

		// RayGen
		dispatch_rays_desc.RayGenerationShaderRecord.StartAddress = gDxrShaderTable.mResource->GetGPUVirtualAddress() + gDxrShaderTable.mEntrySize * gDxrShaderTable.mRayGenOffset;
		dispatch_rays_desc.RayGenerationShaderRecord.SizeInBytes = gDxrShaderTable.mEntrySize;

		// Miss
		dispatch_rays_desc.MissShaderTable.StartAddress = gDxrShaderTable.mResource->GetGPUVirtualAddress() + gDxrShaderTable.mEntrySize * gDxrShaderTable.mMissOffset;
		dispatch_rays_desc.MissShaderTable.StrideInBytes = gDxrShaderTable.mEntrySize;
		dispatch_rays_desc.MissShaderTable.SizeInBytes = gDxrShaderTable.mEntrySize * gDxrShaderTable.mMissCount;

		// HitGroup
		dispatch_rays_desc.HitGroupTable.StartAddress = gDxrShaderTable.mResource->GetGPUVirtualAddress() + gDxrShaderTable.mEntrySize * gDxrShaderTable.mHitGroupOffset;
		dispatch_rays_desc.HitGroupTable.StrideInBytes = gDxrShaderTable.mEntrySize;
		dispatch_rays_desc.HitGroupTable.SizeInBytes = gDxrShaderTable.mEntrySize * gDxrShaderTable.mHitGroupCount;
	}

	// Bind
	gCommandList->SetComputeRootSignature(gDxrEmptyRootSignature);
	gCommandList->SetDescriptorHeaps(1, &gDxrCbvSrvUavHeap);
	gCommandList->SetPipelineState1(gDxrStateObject);

	// Dispatch
	gCommandList->DispatchRays(&dispatch_rays_desc);

	// Output: UAV -> Copy
	gBarrierTransition(gCommandList, gDxrOutputResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

	// RenderTarget: RT -> Dest
	gBarrierTransition(gCommandList, inFrameRenderTargetResource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);

	// Copy output to render target
	gCommandList->CopyResource(inFrameRenderTargetResource, gDxrOutputResource);

	// RenderTarget: Dest -> RT
	gBarrierTransition(gCommandList, inFrameRenderTargetResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
}