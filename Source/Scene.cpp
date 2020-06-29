#include "Scene.h"

#include "Common.h"
#include "PipelineState.h"
#include "ShaderResource.h"
#include "ShaderTable.h"

#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"

Scene gScene;

static D3D12_HEAP_PROPERTIES sGetDefaultHeapProperties()
{
	D3D12_HEAP_PROPERTIES props = {};
	props.Type = D3D12_HEAP_TYPE_DEFAULT;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	return props;
}

static D3D12_HEAP_PROPERTIES sGetUploadHeapProperties()
{
	D3D12_HEAP_PROPERTIES props = {};
	props.Type = D3D12_HEAP_TYPE_UPLOAD;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	return props;
}

static D3D12_RESOURCE_DESC sGetResourceDesc(UINT64 inWidth)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Width = inWidth;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	return desc;
}

static D3D12_RESOURCE_DESC sGetUAVResourceDesc(UINT64 inWidth)
{
	D3D12_RESOURCE_DESC desc = sGetResourceDesc(inWidth);
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	return desc;
}

VertexBuffer::VertexBuffer(void* inData, uint32_t inVertexSize, uint32_t inVertexCount, std::wstring inName)
{
	mVertexCount = inVertexCount;
	mVertexSize = inVertexSize;
	uint64_t size_in_bytes = mVertexSize * mVertexCount;

	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Width = size_in_bytes;

	D3D12_HEAP_PROPERTIES props;
	memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
	props.Type = D3D12_HEAP_TYPE_UPLOAD;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mResource)));
	gSetName(mResource, inName, L".VertextBuffer");

	uint8_t* pData = nullptr;
	mResource->Map(0, nullptr, (void**)&pData);
	memcpy(pData, inData, size_in_bytes);
	mResource->Unmap(0, nullptr);
}

void BLAS::Initialize(std::vector<VertexBufferRef>&& inVertexBuffers)
{
	mVertexBuffers = std::move(inVertexBuffers);

	mDescs.clear();
	for (auto&& vertex_buffer : mVertexBuffers)
	{
		D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
		desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		desc.Triangles.VertexBuffer.StartAddress = vertex_buffer->GetResource()->GetGPUVirtualAddress();
		desc.Triangles.VertexBuffer.StrideInBytes = vertex_buffer->GetVertexSize();
		desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		desc.Triangles.VertexCount = vertex_buffer->GetVertexCount();
		desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
		mDescs.push_back(desc);
	}

	mInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	mInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	mInputs.NumDescs = static_cast<UINT>(mDescs.size());
	mInputs.pGeometryDescs = mDescs.data();
	mInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	gDevice->GetRaytracingAccelerationStructurePrebuildInfo(&mInputs, &info);

	D3D12_HEAP_PROPERTIES props = sGetDefaultHeapProperties();
	D3D12_RESOURCE_DESC desc = sGetUAVResourceDesc(info.ScratchDataSizeInBytes);

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mScratch)));
	gSetName(mScratch, mName, L".BLAS.Scratch");

	desc = sGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
	gSetName(mDest, mName, L".BLAS.Dest");
}

void BLAS::Build(ID3D12GraphicsCommandList4* inCommandList)
{
	if (mBuilt)
		return;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs = mInputs;
	desc.DestAccelerationStructureData = mDest->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = mScratch->GetGPUVirtualAddress();
	inCommandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

	// UAV barrier after build before use
	gBarrierUAV(inCommandList, mDest.Get());

	mBuilt = true;
}

void TLAS::Update(ID3D12GraphicsCommandList4* inCommandList)
{
	UpdateObjectInstances();
	BuildInternal(inCommandList, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE);
}

void TLAS::Build(ID3D12GraphicsCommandList4* inCommandList)
{
	for (auto&& object_instance : mObjectInstances)
		object_instance->GetBLAS()->Build(inCommandList);

	UpdateObjectInstances();
	BuildInternal(inCommandList, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE);
}

void TLAS::BuildInternal(ID3D12GraphicsCommandList4* inCommandList, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS inFlags)
{
	// Render -> Update
	gBarrierUAV(inCommandList, mDest.Get());

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs = mInputs;
	desc.Inputs.Flags |= inFlags;
	desc.Inputs.InstanceDescs = mInstanceDescs->GetGPUVirtualAddress();
	desc.DestAccelerationStructureData = mDest->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = mScratch->GetGPUVirtualAddress();

	if (inFlags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE)
		desc.SourceAccelerationStructureData = mDest->GetGPUVirtualAddress();

	inCommandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

	// Update -> Render
	gBarrierUAV(inCommandList, mDest.Get());
}

void TLAS::Initialize(std::vector<ObjectInstanceRef>&& inObjectInstances)
{
	mObjectInstances = std::move(inObjectInstances);

	mInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	mInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	mInputs.NumDescs = static_cast<UINT>(mObjectInstances.size());
	mInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	gDevice->GetRaytracingAccelerationStructurePrebuildInfo(&mInputs, &info);

	{
		D3D12_HEAP_PROPERTIES props = sGetDefaultHeapProperties();
		D3D12_RESOURCE_DESC desc = sGetUAVResourceDesc(info.ScratchDataSizeInBytes);
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mScratch)));
		gSetName(mScratch, mName, L".TLAS.Scratch");
	}

	{
		D3D12_HEAP_PROPERTIES props = sGetDefaultHeapProperties();
		D3D12_RESOURCE_DESC desc = sGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
		gSetName(mDest, mName, L".TLAS.Dest");
	}

	{
		D3D12_HEAP_PROPERTIES props = sGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = sGetResourceDesc(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mObjectInstances.size());
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mInstanceDescs)));
		gSetName(mInstanceDescs, mName, L".TLAS.InstanceDescs");

		// Map once
		mInstanceDescs->Map(0, nullptr, (void**)&mObjectInstanceDesc);
	}
}

void TLAS::UpdateObjectInstances()
{
	int instance_index = 0;
	for (auto&& object_instance : mObjectInstances)
	{
		object_instance->Update();

		mObjectInstanceDesc[instance_index].InstanceID = object_instance->GetInstanceID();									// This value will be exposed to the shader via InstanceID()
		mObjectInstanceDesc[instance_index].InstanceContributionToHitGroupIndex = object_instance->GetHitGroupIndex();		// Match shader table
		mObjectInstanceDesc[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		memcpy(mObjectInstanceDesc[instance_index].Transform, &object_instance->GetTransform(), sizeof(mObjectInstanceDesc[instance_index].Transform));
		mObjectInstanceDesc[instance_index].AccelerationStructure = object_instance->GetBLAS()->GetGPUVirtualAddress();
		mObjectInstanceDesc[instance_index].InstanceMask = 0xFF;

		instance_index++;
	}
}

void sSetupTestScene(std::vector<ObjectInstanceRef>& object_instances)
{
	{
		// triangle
		float vertices[] =
		{
			+0.000f,	+1.0f,		+0.0f,
			+0.866f,	-0.5f,		+0.0f,
			-0.866f,	-0.5f,		+0.0f,
		};

		std::vector<VertexBufferRef> buffers;
		buffers.push_back(std::make_shared<VertexBuffer>(vertices, uint32_t(sizeof(float) * 3), 3, L"Triangle"));

		BLASRef blas = std::make_shared<BLAS>(L"Triangle");
		blas->Initialize(std::move(buffers));

		// TODO: initialize all materials first to get hit group index?
		for (int i = 0; i < 3; i++)
		{
			ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, glm::mat4(1), 0, i);
			object_instance->SetUpdater([](ObjectInstance* object_instance)
			{
				glm::mat4& transform = object_instance->GetTransform();
				transform = glm::mat4(1.0f);
				transform = glm::translate(transform, glm::vec3(-2.0f + 2.0f * object_instance->GetInstanceID(), 0.f, 0.f)); // Instances in a line
				float rotate_speed = 1.0f;
				transform = glm::rotate(transform, gTime * rotate_speed, glm::vec3(0.f, 1.f, 0.f));
				transform = glm::transpose(transform); // Column-major => Row-major
			});
			object_instances.push_back(object_instance);
		}
	}
	{
		// plane
		float vertices[] =
		{
			-10.0f,		-1.0f,		-10.0f,
			+10.0f,		-1.0f,		+10.0f,
			-10.0f,		-1.0f,		+10.0f,

			-10.0f,		-1.0f,		-10.0f,
			+10.0f,		-1.0f,		-10.0f,
			+10.0f,		-1.0f,		+10.0f,
		};

		std::vector<VertexBufferRef> buffers;
		buffers.push_back(std::make_shared<VertexBuffer>(vertices, uint32_t(sizeof(float) * 3), 6, L"Plane"));

		BLASRef blas = std::make_shared<BLAS>(L"Plane");
		blas->Initialize(std::move(buffers));

		ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, glm::mat4(1), 2, 0);
		object_instances.push_back(object_instance);
	}
}

void Scene::Load(const char* inFilename)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	std::string warn;
	std::string err;
	tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, inFilename);

	if (!warn.empty())
		gDebugPrint(warn.c_str());

	if (!err.empty())
		gDebugPrint(err.c_str());

	mTLAS = std::make_shared<TLAS>(L"Scene");
	std::vector<ObjectInstanceRef> object_instances;
	sSetupTestScene(object_instances);
	mTLAS->Initialize(std::move(object_instances));

	gCreatePipelineState();
	gCreateShaderResource(mTLAS->GetGPUVirtualAddress());
	gCreateShaderTable();
}

void Scene::Unload()
{
	gCleanupShaderResource();
	gCleanupShaderTable();
	gCleanupPipelineState();

	mTLAS = nullptr;
}

void Scene::Build(ID3D12GraphicsCommandList4* inCommandList)
{
	mTLAS->Build(inCommandList);
}

void Scene::Update(ID3D12GraphicsCommandList4* inCommandList)
{
	mTLAS->Update(inCommandList);
}

void Scene::RebuildBinding(std::function<void()> inCallback)
{
	gCleanupShaderTable();
	gCleanupShaderResource();

	if (inCallback)
		inCallback();

	gCreateShaderResource(mTLAS->GetGPUVirtualAddress());
	gCreateShaderTable();
}
