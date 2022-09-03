#include "Common.h"
#include "Scene.h"

void gRaytrace()
{
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
	gSwapChain->GetDesc1(&swap_chain_desc);

	if (gUseDXRRayQueryShader)
	{
		gRenderer.Setup(gDXRRayQueryShader.mData);
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

	// Setup
	Shader::Data shader_data { .mRootSignature = gDXRGlobalRootSignature, .mStateObject = gDXRStateObject };
	gRenderer.Setup(shader_data);

	// Dispatch
	gCommandList->DispatchRays(&dispatch_rays_desc);
}