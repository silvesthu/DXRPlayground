#include "AccelerationStructure.h"
#include "Common.h"

void CreateVertexBuffer(void* inData, uint32_t inVertexSize, uint32_t inVertexCount, VertexBuffer& outVertexBuffer)
{
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
	bufDesc.Width = inVertexSize * inVertexCount;

	D3D12_HEAP_PROPERTIES props;
	memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
	props.Type = D3D12_HEAP_TYPE_UPLOAD;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	gValidate(gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outVertexBuffer.mResource)));
	outVertexBuffer.mVertexCount = inVertexCount;
	outVertexBuffer.mVertexSize = inVertexSize;
	outVertexBuffer.mResource->SetName(L"VertexBuffer");

	uint8_t* pData = nullptr;
	outVertexBuffer.mResource->Map(0, nullptr, (void**)& pData);
	memcpy(pData, inData, inVertexSize * inVertexCount);
	outVertexBuffer.mResource->Unmap(0, nullptr);
}

void CreateVertexBuffer()
{
	// triangle
	float triangle[] =
	{
		0.0f,    1.0f,  0.0f,
		0.866f,  -0.5f, 0.0f,
		-0.866f, -0.5f, 0.0f,
	};
	CreateVertexBuffer(triangle, sizeof(float) * 3, 3, gDxrTriangleVertexBuffer);

	// plane
	float plane[] =
	{
		-8, -1, -8,
		8, -1, 8,
		-8, -1, 8,

		-8, -1, -8,
		8, -1, -8,
		8, -1, 8,
	};
	CreateVertexBuffer(plane, sizeof(float) * 3, 6, gDxrPlaneVertexBuffer);
}

void CleanupVertexBuffer()
{
	gDxrTriangleVertexBuffer.Release();
	gDxrPlaneVertexBuffer.Release();
}

void CreateBottomLevelAccelerationStructure(const VertexBuffer& inVertexBuffer, BottomLevelAccelerationStructure& outBLAS)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = {};
	geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometry_desc.Triangles.VertexBuffer.StartAddress = inVertexBuffer.mResource->GetGPUVirtualAddress();
	geometry_desc.Triangles.VertexBuffer.StrideInBytes = inVertexBuffer.mVertexSize;
	geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometry_desc.Triangles.VertexCount = inVertexBuffer.mVertexCount;
	geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = 1;
	inputs.pGeometryDescs = &geometry_desc;
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
		gValidate(gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&outBLAS.mScratch)));
		outBLAS.mScratch->SetName(L"BLAS Scratch");

		bufDesc.Width = info.ResultDataMaxSizeInBytes;
		gValidate(gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&outBLAS.mDest)));
		outBLAS.mDest->SetName(L"BLAS Dest");
	}

	// Create the bottom level acceleration structure
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
		asDesc.Inputs = inputs;
		asDesc.DestAccelerationStructureData = outBLAS.mDest->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = outBLAS.mScratch->GetGPUVirtualAddress();

		gD3DCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	}

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = outBLAS.mDest;
		gD3DCommandList->ResourceBarrier(1, &uavBarrier);
	}
}

void CreateBottomLevelAccelerationStructure()
{
	CreateBottomLevelAccelerationStructure(gDxrTriangleVertexBuffer, gDxrTriangleBLAS);
	CreateBottomLevelAccelerationStructure(gDxrPlaneVertexBuffer, gDxrPlaneBLAS);
}

void CleanupBottomLevelAccelerationStructure()
{
	gDxrTriangleBLAS.Release();
	gDxrPlaneBLAS.Release();
}

void CreateTopLevelAccelerationStructure()
{
	uint32_t plane_count = 1;
	uint32_t triangle_count = 3;
	uint32_t instance_count = plane_count + triangle_count;

	// Get buffer sizes
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = instance_count;
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
		gValidate(gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&gDxrTopLevelAccelerationStructureScratch)));
		gDxrTopLevelAccelerationStructureScratch->SetName(L"gDxrTopLevelAccelerationStructureScratch");

		bufDesc.Width = info.ResultDataMaxSizeInBytes;
		gValidate(gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&gDxrTopLevelAccelerationStructureDest)));
		gDxrTopLevelAccelerationStructureDest->SetName(L"gDxrTopLevelAccelerationStructureDest");

		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		bufDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instance_count;
		gValidate(gD3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gDxrTopLevelAccelerationStructureInstanceDesc)));
		gDxrTopLevelAccelerationStructureInstanceDesc->SetName(L"gDxrTopLevelAccelerationStructureInstanceDesc");

 		// The instance desc should be inside a buffer, create and map the buffer
 		D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
 		gDxrTopLevelAccelerationStructureInstanceDesc->Map(0, nullptr, (void**)& pInstanceDesc);

		uint32_t instance_index = 0;

		for (uint32_t i = 0; i < plane_count; i++)
		{
			pInstanceDesc[instance_index].InstanceID = i;                            // This value will be exposed to the shader via InstanceID()
			pInstanceDesc[instance_index].InstanceContributionToHitGroupIndex = 0;   // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
			pInstanceDesc[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			glm::mat4 transform = glm::mat4(1.0f);
			memcpy(pInstanceDesc[instance_index].Transform, &transform, sizeof(pInstanceDesc[instance_index].Transform));
			pInstanceDesc[instance_index].AccelerationStructure = gDxrPlaneBLAS.mDest->GetGPUVirtualAddress();
			pInstanceDesc[instance_index].InstanceMask = 0xFF;

			instance_index++;
		}

		for (uint32_t i = 0; i < triangle_count; i++)
		{
			pInstanceDesc[instance_index].InstanceID = i;                            // This value will be exposed to the shader via InstanceID()
			pInstanceDesc[instance_index].InstanceContributionToHitGroupIndex = 0;   // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
			pInstanceDesc[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
			glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f + 2.0f * i, 0.f, 0.f)); // Instances in a line
			transform = glm::transpose(transform); // Column-major => Row-major
			memcpy(pInstanceDesc[instance_index].Transform, &transform, sizeof(pInstanceDesc[instance_index].Transform));
			pInstanceDesc[instance_index].AccelerationStructure = gDxrTriangleBLAS.mDest->GetGPUVirtualAddress();
			pInstanceDesc[instance_index].InstanceMask = 0xFF;

			instance_index++;
		}

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