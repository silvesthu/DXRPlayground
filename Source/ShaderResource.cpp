#include "Common.h"

void gCreateShaderResource(D3D12_GPU_VIRTUAL_ADDRESS inTLASAddress)
{
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
	gSwapChain->GetDesc1(&swap_chain_desc);

	// Create UAV resource
	{
		// Create the output resource. The dimensions and format should match the swap-chain
		// As this resource is used to hold output and to be copied to back-buffer
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

		gValidate(gDevice->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&gDxrOutputResource))); // Starting as copy-source to simplify onFrameRender()
		gDxrOutputResource->SetName(L"gDxrOutputResource");
	}

	// Create a descriptor heap
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = 3;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	gValidate(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gDxrCbvSrvUavHeap)));

	// Fill the descriptor heap
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = gDxrCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();

		{
			// Create the UAV. Referenced in GenerateRayGenLocalRootDesc()
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			gDevice->CreateUnorderedAccessView(gDxrOutputResource, nullptr, &uavDesc, handle);
		}

		{
			// Create the SRV. Referenced in GenerateRayGenLocalRootDesc()
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.RaytracingAccelerationStructure.Location = inTLASAddress;
			
			handle.ptr += gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			gDevice->CreateShaderResourceView(nullptr, &srvDesc, handle);
		}

		{
			// Create the CBV. Referenced in GenerateMissRootDesc()
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = gConstantGPUBuffer->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = gAlignUp((UINT)sizeof(PerFrame), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

			handle.ptr += gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			gDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	// Create ad-hoc constant buffer resource
	{
		D3D12_RESOURCE_DESC resource_desc = {};
		resource_desc.Alignment = 0;
		resource_desc.DepthOrArraySize = 1;
		resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
		resource_desc.Format = DXGI_FORMAT_UNKNOWN;
		resource_desc.Width = gAlignUp((UINT)sizeof(PerFrame), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		resource_desc.Height = 1;
		resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resource_desc.MipLevels = 1;
		resource_desc.SampleDesc.Count = 1;
		resource_desc.SampleDesc.Quality = 0;

		D3D12_HEAP_PROPERTIES heap_props;
		heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
		heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_props.CreationNodeMask = 0;
		heap_props.VisibleNodeMask = 0;

		gValidate(gDevice->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gDxrHitConstantBufferResource)));
		gDxrHitConstantBufferResource->SetName(L"gDxrHitConstantBufferResource");

		glm::vec4 scene_data[] =
		{
			// A
			glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
			glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),
			glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),

			// B
			glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
			glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),
			glm::vec4(1.0f, 0.0f, 1.0f, 1.0f),

			// C
			glm::vec4(1.0f, 0.0f, 1.0f, 1.0f),
			glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),
			glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),
		};

		uint8_t* data;
		gValidate(gDxrHitConstantBufferResource->Map(0, nullptr, (void**)& data));
		memcpy(data, scene_data, sizeof(scene_data));
		gDxrHitConstantBufferResource->Unmap(0, nullptr);
	}
}

void gCleanupShaderResource()
{
	gSafeRelease(gDxrHitConstantBufferResource);
	gSafeRelease(gDxrCbvSrvUavHeap);
	gSafeRelease(gDxrOutputResource);
}
