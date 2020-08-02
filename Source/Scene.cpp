#include "Scene.h"

#include "Common.h"
#include "PipelineState.h"
#include "ShaderResource.h"
#include "ShaderTable.h"

#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"

Scene gScene;

void BLAS::Initialize(D3D12_GPU_VIRTUAL_ADDRESS inVertexBaseAddress, D3D12_GPU_VIRTUAL_ADDRESS inIndexBaseAddress)
{
	mDesc = {};
	mDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	mDesc.Triangles.VertexBuffer.StartAddress = inVertexBaseAddress + mPrimitive->GetVertexOffset() * Primitive::sVertexSize;
	mDesc.Triangles.VertexBuffer.StrideInBytes = Primitive::sVertexSize;
	mDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	mDesc.Triangles.VertexCount = mPrimitive->GetVertexCount();
	if (mPrimitive->GetIndexCount() > 0)
	{
		mDesc.Triangles.IndexBuffer = inIndexBaseAddress + mPrimitive->GetIndexOffset() * Primitive::sIndexSize;
		mDesc.Triangles.IndexCount = mPrimitive->GetIndexCount();
		mDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
	}
	mDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	mInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	mInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	mInputs.NumDescs = 1;
	mInputs.pGeometryDescs = &mDesc;
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
		mInstanceDescs->Map(0, nullptr, (void**)&mInstanceDescsPointer);
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(sizeof(InstanceData) * mObjectInstances.size());
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mInstanceBuffer)));
		gSetName(mDest, mName, L".TLAS.InstanceBuffer");

		// Map once
		mInstanceBuffer->Map(0, nullptr, (void**)&mInstanceBufferPointer);
	}
}

void TLAS::UpdateObjectInstances()
{
	int instance_index = 0;
	for (auto&& object_instance : mObjectInstances)
	{
		object_instance->Update();

		mInstanceDescsPointer[instance_index].InstanceID = instance_index; // This value will be exposed to the shader via InstanceID()
		mInstanceDescsPointer[instance_index].InstanceContributionToHitGroupIndex = object_instance->GetHitGroupIndex(); // Match shader table
		mInstanceDescsPointer[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		memcpy(mInstanceDescsPointer[instance_index].Transform, &object_instance->Transform(), sizeof(mInstanceDescsPointer[instance_index].Transform));
		mInstanceDescsPointer[instance_index].AccelerationStructure = object_instance->GetBLAS()->GetGPUVirtualAddress();
		mInstanceDescsPointer[instance_index].InstanceMask = 0xFF;

		mInstanceBufferPointer[instance_index] = object_instance->Data();

		instance_index++;
	}
}

void Scene::Load(const char* inFilename)
{
	mTLAS = std::make_shared<TLAS>(L"Scene");
	std::vector<ObjectInstanceRef> object_instances;
	std::vector<glm::uint16> indices;
	std::vector<glm::vec3> vertices;

	tinyobj::ObjReader reader;
	reader.ParseFromFile(inFilename);

	if (!reader.Warning().empty())
		gDebugPrint(reader.Warning().c_str());

	if (!reader.Error().empty())
		gDebugPrint(reader.Error().c_str());

	// vertices
	glm::uint32 vertex_offset = (glm::uint32)vertices.size();
	glm::uint32 vertex_count = (glm::uint32)reader.GetAttrib().vertices.size() / 3;
	glm::vec3* vertex_pointer_begin = (glm::vec3*)reader.GetAttrib().vertices.data();
	glm::vec3* vertex_pointer_end = vertex_pointer_begin + vertex_count;
	vertices.insert(vertices.end(), vertex_pointer_begin, vertex_pointer_end);

	// indices
	for (auto&& shape : reader.GetShapes())
	{
		glm::uint32 index_offset = (glm::uint32)indices.size();
		
		const int kNumFaceVerticesTriangle = 3;
		glm::uint32 index_count = (glm::uint32)shape.mesh.num_face_vertices.size() * kNumFaceVerticesTriangle;
		for (size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); face_index++)
		{
			assert(shape.mesh.num_face_vertices[face_index] == kNumFaceVerticesTriangle);
			for (size_t vertex_index = 0; vertex_index < kNumFaceVerticesTriangle; vertex_index++)
			{
				tinyobj::index_t idx = shape.mesh.indices[face_index * kNumFaceVerticesTriangle + vertex_index];
				indices.push_back(uint16_t(idx.vertex_index));
			}
		}

		assert(vertex_count == (glm::uint32)vertices.size() - vertex_offset);
		assert(index_count == (glm::uint32)indices.size() - index_offset);

		std::wstring name(shape.name.begin(), shape.name.end());
		BLASRef blas = std::make_shared<BLAS>(std::make_shared<Primitive>(vertex_offset, vertex_count, index_offset, index_count), name);
		ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, glm::mat4(1), kDefaultHitGroupIndex);
		object_instances.push_back(object_instance);

		if (shape.mesh.material_ids.size() > 0)
		{
			const tinyobj::material_t& material = reader.GetMaterials()[shape.mesh.material_ids[0]];
			object_instance->Data().mAlbedo = glm::vec3(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
			object_instance->Data().mEmission = glm::vec3(material.emission[0], material.emission[1], material.emission[2]);
			object_instance->Data().mReflectance = glm::vec3(material.specular[0], material.specular[1], material.specular[2]);
			object_instance->Data().mRoughness = glm::vec1(material.roughness);
		}
	}

	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(0);
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		{
			desc.Width =  Primitive::sVertexSize * vertices.size();
			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mVertexBuffer)));
			gSetName(mVertexBuffer, L"Scene", L".VertextBuffer");

			uint8_t* pData = nullptr;
			mVertexBuffer->Map(0, nullptr, (void**)&pData);
			memcpy(pData, vertices.data(), desc.Width);
			mVertexBuffer->Unmap(0, nullptr);
		}

		{
			desc.Width = Primitive::sIndexSize * indices.size();
			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mIndexBuffer)));
			gSetName(mIndexBuffer, L"Scene", L".IndexBuffer");

			uint8_t* pData = nullptr;
			mIndexBuffer->Map(0, nullptr, (void**)&pData);
			memcpy(pData, indices.data(), desc.Width);
			mIndexBuffer->Unmap(0, nullptr);
		}

		for (auto&& object_instance : object_instances)
			object_instance->GetBLAS()->Initialize(mVertexBuffer->GetGPUVirtualAddress(), mIndexBuffer->GetGPUVirtualAddress());
	}
	
	mTLAS->Initialize(std::move(object_instances));

	gCreatePipelineState();
	gCreateShaderResource(mTLAS->GetGPUVirtualAddress(), mTLAS->GetInstanceBuffer());
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

	gCreateShaderResource(mTLAS->GetGPUVirtualAddress(), mTLAS->GetInstanceBuffer());
	gCreateShaderTable();
}

void Scene::RebuildShader()
{
	// gCleanupPipelineState(); // Skip cleanup in case rebuild fails
	gCreatePipelineState();

	gCleanupShaderTable();
	gCreateShaderTable();
}