#include "AccelerationStructure.h"
#include "CommonInclude.h"

void CreateShaderResource()
{
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
	gSwapChain->GetDesc1(&swap_chain_desc);

	// Create the output resource. The dimensions and format should match the swap-chain
	// As this resource is used to hold output and copy to back-buffer
	D3D12_RESOURCE_DESC resource_desc = {};
	resource_desc.DepthOrArraySize = 1;
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats can't be used with UAVs. We will convert to sRGB ourselves in the shader
	resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resource_desc.Width = swap_chain_desc.Width;
	resource_desc.Height = swap_chain_desc.Height;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.MipLevels = 1;
	resource_desc.SampleDesc.Count = 1;

	D3D12_HEAP_PROPERTIES heap_props;
	heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
	heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap_props.CreationNodeMask = 0;
	heap_props.VisibleNodeMask = 0;

	gValidate(gD3DDevice->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&gDxrOutputResource))); // Starting as copy-source to simplify onFrameRender()

	// Create an SRV/UAV descriptor heap. Need 2 entries - 1 SRV for the scene and 1 UAV for the output
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 2;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	gValidate(gD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gDxrSrvUavHeap)));

	// Create the UAV. Based on the root signature we created it should be the first entry
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	gD3DDevice->CreateUnorderedAccessView(gDxrOutputResource, nullptr, &uavDesc, gDxrSrvUavHeap->GetCPUDescriptorHandleForHeapStart());

	// Create the TLAS SRV right after the UAV. Note that we are using a different SRV desc here
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = gDxrTopLevelAccelerationStructureDest->GetGPUVirtualAddress();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = gDxrSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
	srvHandle.ptr += gD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	gD3DDevice->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
}

void CleanupShaderResource()
{
	gSafeRelease(gDxrSrvUavHeap);
	gSafeRelease(gDxrOutputResource);
}
