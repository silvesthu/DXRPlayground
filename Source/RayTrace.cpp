#include "AccelerationStructure.h"
#include "Common.h"

void RayTrace(ID3D12Resource* inFrameRenderTargetResource)
{
	// Output - Copy -> UAV
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = gDxrOutputResource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		gD3DCommandList->ResourceBarrier(1, &barrier);
	}

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
	gSwapChain->GetDesc1(&swap_chain_desc);

	D3D12_DISPATCH_RAYS_DESC dispatch_rays_desc = {};
	dispatch_rays_desc.Width = swap_chain_desc.Width;
	dispatch_rays_desc.Height = swap_chain_desc.Height;
	dispatch_rays_desc.Depth = 1;

	// RayGen is the first entry in the shader-table
	dispatch_rays_desc.RayGenerationShaderRecord.StartAddress = gDxrShaderTable->GetGPUVirtualAddress() + 0 * gDxrShaderTableEntrySize;
	dispatch_rays_desc.RayGenerationShaderRecord.SizeInBytes = gDxrShaderTableEntrySize;

	// Miss is the second entry in the shader-table
	size_t missOffset = 1 * gDxrShaderTableEntrySize;
	dispatch_rays_desc.MissShaderTable.StartAddress = gDxrShaderTable->GetGPUVirtualAddress() + missOffset;
	dispatch_rays_desc.MissShaderTable.StrideInBytes = gDxrShaderTableEntrySize;
	dispatch_rays_desc.MissShaderTable.SizeInBytes = gDxrShaderTableEntrySize;

	// Hit is the third entry in the shader-table
	size_t hitOffset = 2 * gDxrShaderTableEntrySize;
	dispatch_rays_desc.HitGroupTable.StartAddress = gDxrShaderTable->GetGPUVirtualAddress() + hitOffset;
	dispatch_rays_desc.HitGroupTable.StrideInBytes = gDxrShaderTableEntrySize;
	dispatch_rays_desc.HitGroupTable.SizeInBytes = gDxrShaderTableEntrySize;

	// Bind the empty root signature
	gD3DCommandList->SetComputeRootSignature(gDxrEmptyRootSignature);

	// Dispatch
	gD3DCommandList->SetDescriptorHeaps(1, &gDxrCbvSrvUavHeap);
	gD3DCommandList->SetPipelineState1(gDxrStateObject);
	gD3DCommandList->DispatchRays(&dispatch_rays_desc);

	// [TODO] Should batch barrier together

	// Output: UAV -> Copy
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = gDxrOutputResource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		gD3DCommandList->ResourceBarrier(1, &barrier);
	}

	// RenderTarget: RT -> Dest
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = inFrameRenderTargetResource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		gD3DCommandList->ResourceBarrier(1, &barrier);
	}

	// Copy output to render target
	gD3DCommandList->CopyResource(inFrameRenderTargetResource, gDxrOutputResource);

	// RenderTarget: Dest -> RT
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = inFrameRenderTargetResource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		gD3DCommandList->ResourceBarrier(1, &barrier);
	}
}