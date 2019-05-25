#include "AccelerationStructure.h"
#include "Common.h"

void CreateVertexBuffer()
{
	std::array<float, 9> vertices =
	{
		0.0f,    1.0f,  0.0f,
		0.866f,  -0.5f, 0.0f,
		-0.866f, -0.5f, 0.0f,
	};

	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Alignment = 0;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	bufDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufDesc.Height = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.SampleDesc.Quality = 0;
	bufDesc.Width = sizeof(vertices);

	D3D12_HEAP_PROPERTIES props;
	memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
	props.Type = D3D12_HEAP_TYPE_UPLOAD;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gDxrVertexBuffer));

	uint8_t* pData = nullptr;
	gDxrVertexBuffer->Map(0, nullptr, (void**)& pData);
	memcpy(pData, vertices.data(), sizeof(vertices));
	gDxrVertexBuffer->Unmap(0, nullptr);
}

void CleanupVertexBuffer()
{
	if (gDxrVertexBuffer) { gDxrVertexBuffer->Release(); gDxrVertexBuffer = NULL; }
}

void CreateBottomLevelAccelerationStructure()
{
	D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
	geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geomDesc.Triangles.VertexBuffer.StartAddress = gDxrVertexBuffer->GetGPUVirtualAddress();
	geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
	geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geomDesc.Triangles.VertexCount = 3;
	geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = 1;
	inputs.pGeometryDescs = &geomDesc;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	// Create the buffers. They need to support UAV, and since we are going to immediately use them, we create them with an unordered-access state
	{
		D3D12_HEAP_PROPERTIES props = {};
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		gD3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Alignment = 0;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.Height = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.MipLevels = 1;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.SampleDesc.Quality = 0;
		bufDesc.Width = info.ScratchDataSizeInBytes;
		gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&gDxrBottomLevelAccelerationStructureScratch));

		bufDesc.Width = info.ResultDataMaxSizeInBytes;
		gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&gDxrBottomLevelAccelerationStructureDest));
	}

	// Create the bottom level acceleration structure
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
		asDesc.Inputs = inputs;
		asDesc.DestAccelerationStructureData = gDxrBottomLevelAccelerationStructureDest->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = gDxrBottomLevelAccelerationStructureScratch->GetGPUVirtualAddress();

		gD3DCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	}

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = gDxrBottomLevelAccelerationStructureDest;
		gD3DCommandList->ResourceBarrier(1, &uavBarrier);
	}
}

void CleanupBottomLevelAccelerationStructure()
{
	gSafeRelease(gDxrBottomLevelAccelerationStructureScratch);
	gSafeRelease(gDxrBottomLevelAccelerationStructureDest);
}

void CreateTopLevelAccelerationStructure()
{
	// Get buffer sizes
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = 1;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	// Create the buffers
	{
		D3D12_HEAP_PROPERTIES props = {};
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		gD3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Alignment = 0;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.Height = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.MipLevels = 1;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.SampleDesc.Quality = 0;
		bufDesc.Width = info.ScratchDataSizeInBytes;
		gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&gDxrTopLevelAccelerationStructureScratch));

		bufDesc.Width = info.ResultDataMaxSizeInBytes;
		gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&gDxrTopLevelAccelerationStructureDest));

		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		bufDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
		gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gDxrTopLevelAccelerationStructureInstanceDesc));

		// The instance desc should be inside a buffer, create and map the buffer
		D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
		gDxrTopLevelAccelerationStructureInstanceDesc->Map(0, nullptr, (void**)& pInstanceDesc);

		// Initialize the instance desc. We only have a single instance
		pInstanceDesc->InstanceID = 0;                            // This value will be exposed to the shader via InstanceID()
		pInstanceDesc->InstanceContributionToHitGroupIndex = 0;   // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
		pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		float m[] =
		{
			1,0,0,0,
			0,1,0,0,
			0,0,1,0,
			0,0,0,1,
		}; // Identity matrix
		memcpy(pInstanceDesc->Transform, &m, sizeof(pInstanceDesc->Transform));
		pInstanceDesc->AccelerationStructure = gDxrBottomLevelAccelerationStructureDest->GetGPUVirtualAddress();
		pInstanceDesc->InstanceMask = 0xFF;

		// Unmap
		gDxrTopLevelAccelerationStructureInstanceDesc->Unmap(0, nullptr);
	}

	// Create the top level acceleration structure
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
		asDesc.Inputs = inputs;
		asDesc.Inputs.InstanceDescs = gDxrTopLevelAccelerationStructureInstanceDesc->GetGPUVirtualAddress();
		asDesc.DestAccelerationStructureData = gDxrTopLevelAccelerationStructureDest->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = gDxrTopLevelAccelerationStructureScratch->GetGPUVirtualAddress();

		gD3DCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	}

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = gDxrTopLevelAccelerationStructureDest;
		gD3DCommandList->ResourceBarrier(1, &uavBarrier);
	}
}

void CleanupTopLevelAccelerationStructure()
{
	gSafeRelease(gDxrTopLevelAccelerationStructureScratch);
	gSafeRelease(gDxrTopLevelAccelerationStructureDest);
	gSafeRelease(gDxrTopLevelAccelerationStructureInstanceDesc);
}

void ExecuteAccelerationStructureCreation()
{
	// The tutorial doesn't have any resource lifetime management, so we flush and sync here. 
	// This is not required by the DXR spec - you can submit the list whenever you like as long as you take care of the resources lifetime.

	gD3DCommandList->Close();
	gD3DCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList * const*)& gD3DCommandList);
	uint64_t one_shot_fence_value = 0xff;
	gD3DCommandQueue->Signal(gFence, one_shot_fence_value);
	gFence->SetEventOnCompletion(one_shot_fence_value, gFenceEvent);
	WaitForSingleObject(gFenceEvent, INFINITE);

	// Note that the CommandList is closed here, it will be reset at beginning of render loop
}