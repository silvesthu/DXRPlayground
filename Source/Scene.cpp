#include "Scene.h"

#include "Common.h"
#include "PipelineState.h"
#include "ShaderResource.h"
#include "ShaderTable.h"

#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"

Scene gScene;

Primitive::Primitive(const void* inVertexData, uint32_t inVertexSize, uint32_t inVertexCount, const void* inIndexData, uint32_t inIndexCount, std::wstring inName)
{
	mVertexCount = inVertexCount;
	mVertexSize = inVertexSize;
	mIndexCount = inIndexCount;

	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(0);
	D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

	{
		desc.Width = (UINT64)GetVertexSize() * GetVertexCount();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mVertexBufferResource)));
		gSetName(mVertexBufferResource, inName, L".VertextBuffer");

		uint8_t* pData = nullptr;
		mVertexBufferResource->Map(0, nullptr, (void**)&pData);
		memcpy(pData, inVertexData, desc.Width);
		mVertexBufferResource->Unmap(0, nullptr);
	}

	{
		desc.Width = (UINT64)GetIndexSize() * GetIndexCount();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mIndexBufferResource)));
		gSetName(mIndexBufferResource, inName, L".IndexBuffer");

		uint8_t* pData = nullptr;
		mIndexBufferResource->Map(0, nullptr, (void**)&pData);
		memcpy(pData, inIndexData, desc.Width);
		mIndexBufferResource->Unmap(0, nullptr);
	}
}

void BLAS::Initialize(std::vector<PrimitiveRef>&& inPrimitives)
{
	mPrimitives = std::move(inPrimitives);

	mDescs.clear();
	for (auto&& primitive : mPrimitives)
	{
		D3D12_RAYTRACING_GEOMETRY_DESC desc = {};
		desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		desc.Triangles.VertexBuffer.StartAddress = primitive->GetResource()->GetGPUVirtualAddress();
		desc.Triangles.VertexBuffer.StrideInBytes = primitive->GetVertexSize();
		desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		desc.Triangles.VertexCount = primitive->GetVertexCount();
		if (primitive->GetIndexCount() > 0)
		{
			desc.Triangles.IndexBuffer = primitive->GetIndexBufferResource()->GetGPUVirtualAddress();
			desc.Triangles.IndexCount = primitive->GetIndexCount();
			desc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
		}
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

	D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
	D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ScratchDataSizeInBytes);

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mScratch)));
	gSetName(mScratch, mName, L".BLAS.Scratch");

	desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
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
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ScratchDataSizeInBytes);
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mScratch)));
		gSetName(mScratch, mName, L".TLAS.Scratch");
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
		gSetName(mDest, mName, L".TLAS.Dest");
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mObjectInstances.size());
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

void sSetupTestScene(std::vector<ObjectInstanceRef>& ioObjectInstances)
{
	{
		// triangle
		float vertices[] =
		{
			+0.000f,	+1.0f,		+0.0f,
			+0.866f,	-0.5f,		+0.0f,
			-0.866f,	-0.5f,		+0.0f,
		};

		uint16_t indices[] =
		{
			0,
			1,
			2
		};

		std::vector<PrimitiveRef> buffers;
		buffers.push_back(std::make_shared<Primitive>(vertices, uint32_t(sizeof(float) * 3), 3, indices, 3, L"Triangle"));

		BLASRef blas = std::make_shared<BLAS>(L"Triangle");
		blas->Initialize(std::move(buffers));

		// TODO: initialize all materials first to get hit group index?
		for (int i = 0; i < 3; i++)
		{
			ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, glm::mat4(1), kUVHitGroup, i);
			object_instance->SetUpdater([](ObjectInstance* object_instance)
			{
				glm::mat4& transform = object_instance->GetTransform();
				transform = glm::mat4(1.0f);
				transform = glm::translate(transform, glm::vec3(-2.0f + 2.0f * object_instance->GetInstanceID(), 0.f, 0.f)); // Instances in a line
				float rotate_speed = 1.0f;
				transform = glm::rotate(transform, gTime * rotate_speed, glm::vec3(0.f, 1.f, 0.f));
				transform = glm::transpose(transform); // Column-major => Row-major
			});
			ioObjectInstances.push_back(object_instance);
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

		uint16_t indices[] =
		{
			0,
			1,
			2,

			3,
			4,
			5,
		};

		std::vector<PrimitiveRef> buffers;
		buffers.push_back(std::make_shared<Primitive>(vertices, uint32_t(sizeof(float) * 3), 6, indices, 6, L"Plane"));

		BLASRef blas = std::make_shared<BLAS>(L"Plane");
		blas->Initialize(std::move(buffers));

		ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, glm::mat4(1), kGreyHitGroup, 0);
		ioObjectInstances.push_back(object_instance);
	}
}

void Scene::Load(const char* inFilename)
{
	mTLAS = std::make_shared<TLAS>(L"Scene");
	std::vector<ObjectInstanceRef> object_instances;

	if (inFilename != nullptr)
	{		
		tinyobj::ObjReader reader;
		reader.ParseFromFile(inFilename);

		if (!reader.Warning().empty())
			gDebugPrint(reader.Warning().c_str());

		if (!reader.Error().empty())
			gDebugPrint(reader.Error().c_str());

		for (auto&& shape : reader.GetShapes())
		{
			std::vector<uint16_t> indices;
			for (size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); face_index++)
			{
				const int kNumFaceVerticesTriangle = 3;
				assert(shape.mesh.num_face_vertices[face_index] == kNumFaceVerticesTriangle);
				for (size_t vertex_index = 0; vertex_index < kNumFaceVerticesTriangle; vertex_index++)
				{
					tinyobj::index_t idx = shape.mesh.indices[face_index * kNumFaceVerticesTriangle + vertex_index];
					indices.push_back(uint16_t(idx.vertex_index));

					// tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
					// tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
					// tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
					// tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
					// tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
					// tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];
					// tinyobj::real_t tx = attrib.texcoords[2 * idx.texcoord_index + 0];
					// tinyobj::real_t ty = attrib.texcoords[2 * idx.texcoord_index + 1];
					// tinyobj::real_t red = attrib.colors[3*idx.vertex_index+0];
					// tinyobj::real_t green = attrib.colors[3*idx.vertex_index+1];
					// tinyobj::real_t blue = attrib.colors[3*idx.vertex_index+2];
				}
			}

			std::wstring name(shape.name.begin(), shape.name.end());

			std::vector<PrimitiveRef> buffers;
			buffers.push_back(std::make_shared<Primitive>(reader.GetAttrib().vertices.data(), uint32_t(sizeof(float) * 3), uint32_t(reader.GetAttrib().vertices.size()), indices.data(), uint32_t(indices.size()), name));

			BLASRef blas = std::make_shared<BLAS>(name);
			blas->Initialize(std::move(buffers));

			ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, glm::mat4(1), kUVHitGroup, 0);
			object_instances.push_back(object_instance);
		}
	}
	else
		sSetupTestScene(object_instances);
	
	mTLAS->Initialize(std::move(object_instances));

	gCreatePipelineState();
	gCreateShaderResource(mTLAS->GetGPUVirtualAddress());
	gCreateShaderTable();
}

void Scene::Unload()
{
	gCleanupShaderTable();
	gCleanupShaderResource();
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
