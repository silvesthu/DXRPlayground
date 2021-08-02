#include "Common.h"
#include "Scene.h"
#include "Atmosphere.h"

void gRaytrace()
{
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
	gSwapChain->GetDesc1(&swap_chain_desc);

	if (gUseDXRInlineShader)
	{
		// Inline Raytracing
		gDXRInlineShader.SetupCompute(gScene.GetDXRDescriptorHeap());
		gCommandList->Dispatch(swap_chain_desc.Width / 8, swap_chain_desc.Height / 8, 1);
		return;
	}

	// Setup D3D12_DISPATCH_RAYS_DESC
	D3D12_DISPATCH_RAYS_DESC dispatch_rays_desc = {};
	{
		dispatch_rays_desc.Width = swap_chain_desc.Width;
		dispatch_rays_desc.Height = swap_chain_desc.Height;
		dispatch_rays_desc.Depth = 1;

		// RayGen
		dispatch_rays_desc.RayGenerationShaderRecord.StartAddress = gDXRShaderTable.mResource->GetGPUVirtualAddress() + gDXRShaderTable.mEntrySize * gDXRShaderTable.mRayGenOffset;
		dispatch_rays_desc.RayGenerationShaderRecord.SizeInBytes = gDXRShaderTable.mEntrySize;

		// Miss
		dispatch_rays_desc.MissShaderTable.StartAddress = gDXRShaderTable.mResource->GetGPUVirtualAddress() + gDXRShaderTable.mEntrySize * gDXRShaderTable.mMissOffset;
		dispatch_rays_desc.MissShaderTable.StrideInBytes = gDXRShaderTable.mEntrySize;
		dispatch_rays_desc.MissShaderTable.SizeInBytes = gDXRShaderTable.mEntrySize * gDXRShaderTable.mMissCount;

		// HitGroup
		dispatch_rays_desc.HitGroupTable.StartAddress = gDXRShaderTable.mResource->GetGPUVirtualAddress() + gDXRShaderTable.mEntrySize * gDXRShaderTable.mHitGroupOffset;
		dispatch_rays_desc.HitGroupTable.StrideInBytes = gDXRShaderTable.mEntrySize;
		dispatch_rays_desc.HitGroupTable.SizeInBytes = gDXRShaderTable.mEntrySize * gDXRShaderTable.mHitGroupCount;
	}

	// Bind
	gCommandList->SetComputeRootSignature(gDXRGlobalRootSignature.Get());
	ID3D12DescriptorHeap* descriptor_heap = gScene.GetDXRDescriptorHeap();
	gCommandList->SetDescriptorHeaps(1, &descriptor_heap);
	gCommandList->SetComputeRootDescriptorTable(0, gScene.GetDXRDescriptorHeap()->GetGPUDescriptorHandleForHeapStart());
	gCommandList->SetPipelineState1(gDXRStateObject.Get());

	// Dispatch
	gCommandList->DispatchRays(&dispatch_rays_desc);
}