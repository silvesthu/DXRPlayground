#include "Common.h"

void gCreateShaderResource(D3D12_GPU_VIRTUAL_ADDRESS inTLASAddress)
{
	// Raytrace output (UAV)
	{
		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
		gSwapChain->GetDesc1(&swap_chain_desc);

		D3D12_RESOURCE_DESC resource_desc = gGetTextureResourceDesc(swap_chain_desc.Width, swap_chain_desc.Height, swap_chain_desc.Format);
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&gDxrOutputResource)));
		gDxrOutputResource->SetName(L"gDxrOutputResource");
	}

	// DescriptorHeap
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 64;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		gValidate(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gDxrCbvSrvUavHeap)));

		D3D12_CPU_DESCRIPTOR_HANDLE handle = gDxrCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
		auto increment_handle = [&]() { handle.ptr += gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); };

		// uav
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			gDevice->CreateUnorderedAccessView(gDxrOutputResource, nullptr, &uavDesc, handle);
		}

		increment_handle();

		// srv
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.RaytracingAccelerationStructure.Location = inTLASAddress;			
			gDevice->CreateShaderResourceView(nullptr, &srvDesc, handle);
		}

		increment_handle();

		// cbv
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = gConstantGPUBuffer->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = gAlignUp((UINT)sizeof(PerFrame), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
			gDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}
}

void gCleanupShaderResource()
{
	gSafeRelease(gDxrCbvSrvUavHeap);
	gSafeRelease(gDxrOutputResource);
}
