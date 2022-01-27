#include "Scene.h"

#include "Common.h"
#include "PipelineState.h"
#include "ShaderTable.h"

#include "Atmosphere.h"
#include "Cloud.h"

#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "Thirdparty/tinygltf/tiny_gltf.h"

Scene gScene;

void BLAS::Initialize(D3D12_GPU_VIRTUAL_ADDRESS inVertexBaseAddress, D3D12_GPU_VIRTUAL_ADDRESS inIndexBaseAddress)
{
	mDesc = {};
	mDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	mDesc.Triangles.VertexBuffer.StartAddress = inVertexBaseAddress + mPrimitive->GetVertexOffset() * sizeof(Scene::VertexType);
	mDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Scene::VertexType);
	mDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	mDesc.Triangles.VertexCount = mPrimitive->GetVertexCount();
	if (mPrimitive->GetIndexCount() > 0)
	{
		mDesc.Triangles.IndexBuffer = inIndexBaseAddress + mPrimitive->GetIndexOffset() * sizeof(Scene::IndexType);
		mDesc.Triangles.IndexCount = mPrimitive->GetIndexCount();
		mDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
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

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mScratch)));
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
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mScratch)));
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
		mInstanceDescs->Map(0, nullptr, reinterpret_cast<void**>(&mInstanceDescsPointer));
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(sizeof(InstanceData) * mObjectInstances.size());
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mInstanceBuffer)));
		gSetName(mInstanceBuffer, mName, L".TLAS.InstanceBuffer");

		// Map once
		mInstanceBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mInstanceBufferPointer));
	}
}

void TLAS::UpdateObjectInstances()
{
	int instance_index = 0;
	for (auto&& object_instance : mObjectInstances)
	{
		object_instance->Update();

		glm::mat4x4 transform = glm::transpose(object_instance->Transform());
		memcpy(mInstanceDescsPointer[instance_index].Transform, &transform, sizeof(mInstanceDescsPointer[instance_index].Transform));

		mInstanceDescsPointer[instance_index].InstanceID = instance_index; // This value will be exposed to the shader via InstanceID()
		mInstanceDescsPointer[instance_index].InstanceMask = 0xFF;
		mInstanceDescsPointer[instance_index].InstanceContributionToHitGroupIndex = object_instance->GetHitGroupIndex(); // Match shader table
		mInstanceDescsPointer[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		mInstanceDescsPointer[instance_index].AccelerationStructure = object_instance->GetBLAS()->GetGPUVirtualAddress();

		mInstanceBufferPointer[instance_index] = object_instance->Data();

		instance_index++;
	}
}

void Scene::Load(const char* inFilename, const glm::mat4x4& inTransform)
{
	std::vector<ObjectInstanceRef> object_instances;
	std::vector<IndexType> indices;
	std::vector<VertexType> vertices;
	std::vector<NormalType> normals;
	std::vector<UVType> uvs;

	if (inFilename == nullptr || !std::filesystem::exists(inFilename))
	{
		// Dummy scene
		indices.push_back(0);
		indices.push_back(0);
		indices.push_back(0);

		vertices.push_back(VertexType(0, 0, 0));
		normals.push_back(NormalType(0, 0, 0));
		uvs.push_back(UVType(0, 0));

		BLASRef blas = std::make_shared<BLAS>(std::make_shared<Primitive>(0, 1, 0, 3), L"Dummy");
		ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, glm::mat4(1), kDefaultHitGroupIndex);
		object_instances.push_back(object_instance);
	}
	else
	{
		// Load from file
		tinyobj::ObjReader reader;
		reader.ParseFromFile(inFilename);

		if (!reader.Warning().empty())
			gDebugPrint(reader.Warning().c_str());

		if (!reader.Error().empty())
			gDebugPrint(reader.Error().c_str());

		// Fetch indices, attributes
		glm::uint32 index = 0;
		for (auto&& shape : reader.GetShapes())
		{
			glm::uint32 index_offset = static_cast<glm::uint32>(indices.size());

			const int kNumFaceVerticesTriangle = 3;
			glm::uint32 index_count = static_cast<glm::uint32>(shape.mesh.num_face_vertices.size()) * kNumFaceVerticesTriangle;
			for (size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); face_index++)
			{
				assert(shape.mesh.num_face_vertices[face_index] == kNumFaceVerticesTriangle);
				for (size_t vertex_index = 0; vertex_index < kNumFaceVerticesTriangle; vertex_index++)
				{
					tinyobj::index_t idx = shape.mesh.indices[face_index * kNumFaceVerticesTriangle + vertex_index];
					indices.push_back(static_cast<IndexType>(index++));

					vertices.push_back(VertexType(
						reader.GetAttrib().vertices[3 * idx.vertex_index + 0],
						reader.GetAttrib().vertices[3 * idx.vertex_index + 1],
						reader.GetAttrib().vertices[3 * idx.vertex_index + 2]
					));

					normals.push_back(NormalType(
						reader.GetAttrib().normals[3 * idx.normal_index + 0],
						reader.GetAttrib().normals[3 * idx.normal_index + 1],
						reader.GetAttrib().normals[3 * idx.normal_index + 2]
					));

					if (idx.texcoord_index == -1)
					{
						// Dummy uv if not available
						uvs.push_back(UVType(0, 0));
					}
					else
					{
						uvs.push_back(UVType(
							reader.GetAttrib().texcoords[2 * idx.texcoord_index + 0],
							reader.GetAttrib().texcoords[2 * idx.texcoord_index + 1]
						));
					}
				}
			}

			// trivial - index count == vertex count
			glm::uint32 vertex_offset = 0;
			glm::uint32 vertex_count = index_count;

			std::wstring name(shape.name.begin(), shape.name.end());
			BLASRef blas = std::make_shared<BLAS>(std::make_shared<Primitive>(vertex_offset, vertex_count, index_offset, index_count), name);
			ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, inTransform, kDefaultHitGroupIndex);
			object_instances.push_back(object_instance);

			if (shape.mesh.material_ids.size() > 0)
			{
				if (shape.mesh.material_ids[0] == -1)
				{
					// Dummy material
					object_instance->Data().mAlbedo = glm::vec3(0.1f, 0.1f, 0.1f);
					object_instance->Data().mEmission = glm::vec3(0.0f, 0.0f, 0.0f);
					object_instance->Data().mReflectance = glm::vec3(0.0f, 0.0f, 0.0f);
					object_instance->Data().mRoughness = 1.0f;
				}
				else
				{
					const tinyobj::material_t& material = reader.GetMaterials()[shape.mesh.material_ids[0]];
					object_instance->Data().mAlbedo = glm::vec3(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
					object_instance->Data().mEmission = glm::vec3(material.emission[0], material.emission[1], material.emission[2]);
					object_instance->Data().mReflectance = glm::vec3(material.specular[0], material.specular[1], material.specular[2]);
					object_instance->Data().mRoughness = material.roughness;
				}

				object_instance->Data().mIndexOffset = index_offset;
				object_instance->Data().mVertexOffset = vertex_offset;
			}
		}
	}

	// Construct acceleration structure
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(0);
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		{
			desc.Width = sizeof(IndexType) * indices.size();
			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mIndexBuffer)));
			gSetName(mIndexBuffer, L"Scene", L".IndexBuffer");

			uint8_t* pData = nullptr;
			mIndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			memcpy(pData, indices.data(), desc.Width);
			mIndexBuffer->Unmap(0, nullptr);
		}

		{
			desc.Width =  sizeof(VertexType) * vertices.size();
			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mVertexBuffer)));
			gSetName(mVertexBuffer, L"Scene", L".VertexBuffer");

			uint8_t* pData = nullptr;
			mVertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			memcpy(pData, vertices.data(), desc.Width);
			mVertexBuffer->Unmap(0, nullptr);
		}

		{
			desc.Width = sizeof(NormalType) * normals.size();
			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mNormalBuffer)));
			gSetName(mNormalBuffer, L"Scene", L".NormalBuffer");

			uint8_t* pData = nullptr;
			mNormalBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			memcpy(pData, normals.data(), desc.Width);
			mNormalBuffer->Unmap(0, nullptr);
		}

		{
			desc.Width = sizeof(UVType) * uvs.size();
			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mUVBuffer)));
			gSetName(mUVBuffer, L"Scene", L".UVBuffer");

			uint8_t* pData = nullptr;
			mUVBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			memcpy(pData, uvs.data(), desc.Width);
			mUVBuffer->Unmap(0, nullptr);
		}

		for (auto&& object_instance : object_instances)
			object_instance->GetBLAS()->Initialize(mVertexBuffer->GetGPUVirtualAddress(), mIndexBuffer->GetGPUVirtualAddress());

		mTLAS = std::make_shared<TLAS>(L"Scene");
		mTLAS->Initialize(std::move(object_instances));
	}

	gCreatePipelineState();
	CreateShaderResource();
	gCreateShaderTable();
}

void Scene::Unload()
{
	gCleanupShaderTable();
	CleanupShaderResource();
	gCleanupPipelineState();

	mTLAS = nullptr;

	mNormalBuffer = nullptr;
	mIndexBuffer = nullptr;
	mVertexBuffer = nullptr;
	mUVBuffer = nullptr;
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

	if (inCallback)
		inCallback();

	CreateShaderResource();
	gCreateShaderTable();
}

void Scene::RebuildShader()
{
	// gCleanupPipelineState(); // Skip cleanup in case rebuild fails
	gCreatePipelineState();

	gCleanupShaderTable();
	gCreateShaderTable();
}

void Scene::CreateShaderResource()
{
	// Raytrace output (UAV)
	{
		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
		gSwapChain->GetDesc1(&swap_chain_desc);

		D3D12_RESOURCE_DESC resource_desc = gGetTextureResourceDesc(swap_chain_desc.Width, swap_chain_desc.Height, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mOutputResource)));
		mOutputResource->SetName(L"Scene.OutputResource");
	}

	// Composite 
	{
		gDiffTexture2DShader.InitializeDescriptors({});
		gDiffTexture3DShader.InitializeDescriptors({});
	}

	// Composite 
	{
		gCompositeShader.InitializeDescriptors(
			{
				gConstantGPUBuffer.Get(),
				mOutputResource.Get()
			});
	}

	// DXR DescriptorHeap
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 64;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		gValidate(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mDXRDescriptorHeap)));
	}

	// DXR DescriptorTable
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = mDXRDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		UINT increment_size = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// u0
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			gDevice->CreateUnorderedAccessView(mOutputResource.Get(), nullptr, &desc, handle);

			handle.ptr += increment_size;
		}

		// b0
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			desc.BufferLocation = gConstantGPUBuffer->GetGPUVirtualAddress();
			desc.SizeInBytes = gAlignUp((UINT)sizeof(PerFrameConstants), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
			gDevice->CreateConstantBufferView(&desc, handle);

			handle.ptr += increment_size;
		}

		// t0
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.RaytracingAccelerationStructure.Location = mTLAS->GetGPUVirtualAddress();
			gDevice->CreateShaderResourceView(nullptr, &desc, handle);

			handle.ptr += increment_size;
		}

		// t1
		{
			D3D12_RESOURCE_DESC resource_desc = mTLAS->GetInstanceBuffer()->GetDesc();
			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.Buffer.NumElements = static_cast<UINT>(resource_desc.Width / sizeof(InstanceData));
			desc.Buffer.StructureByteStride = sizeof(InstanceData);
			desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			gDevice->CreateShaderResourceView(mTLAS->GetInstanceBuffer(), &desc, handle);

			handle.ptr += increment_size;
		}

		// t2
		{
			D3D12_RESOURCE_DESC resource_desc = mIndexBuffer->GetDesc();
			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.Buffer.NumElements = static_cast<UINT>(resource_desc.Width / sizeof(Scene::IndexType));
			desc.Buffer.StructureByteStride = sizeof(Scene::IndexType);
			gDevice->CreateShaderResourceView(mIndexBuffer.Get(), &desc, handle);

			handle.ptr += increment_size;
		}

		// t3
		{
			D3D12_RESOURCE_DESC resource_desc = mVertexBuffer->GetDesc();
			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.Buffer.NumElements = static_cast<UINT>(resource_desc.Width / sizeof(Scene::VertexType));
			desc.Buffer.StructureByteStride = sizeof(Scene::VertexType);
			gDevice->CreateShaderResourceView(mVertexBuffer.Get(), &desc, handle);

			handle.ptr += increment_size;
		}

		// t4
		{
			D3D12_RESOURCE_DESC resource_desc = mNormalBuffer->GetDesc();
			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.Buffer.NumElements = static_cast<UINT>(resource_desc.Width / sizeof(Scene::NormalType));
			desc.Buffer.StructureByteStride = sizeof(Scene::NormalType);
			gDevice->CreateShaderResourceView(mNormalBuffer.Get(), &desc, handle);

			handle.ptr += increment_size;
		}

		// b, space2 - PrecomputedAtmosphere
		if (gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer != nullptr)
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			desc.BufferLocation = gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->GetGPUVirtualAddress();
			desc.SizeInBytes = gAlignUp(static_cast<UINT>(sizeof(AtmosphereConstants)), static_cast<UINT>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			gDevice->CreateConstantBufferView(&desc, handle);

			handle.ptr += increment_size;
		}

		// t, space2 - PrecomputedAtmosphere
		for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mTextures)
		{
			if (texture->mResource == nullptr)
				continue;

			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = texture->mFormat;
			srv_desc.ViewDimension = texture->mDepth == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE3D;
			srv_desc.Texture2D.MipLevels = static_cast<UINT>(-1);
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			gDevice->CreateShaderResourceView(texture->mResource.Get(), &srv_desc, handle);

			handle.ptr += increment_size;
		}

		// b, space3 - Cloud
		if (gCloudResources.mConstantUploadBuffer != nullptr)
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			desc.BufferLocation = gCloudResources.mConstantUploadBuffer->GetGPUVirtualAddress();
			desc.SizeInBytes = gAlignUp(static_cast<UINT>(sizeof(Cloud)), static_cast<UINT>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			gDevice->CreateConstantBufferView(&desc, handle);

			handle.ptr += increment_size;
		}

		// t, space3 - Cloud
		for (auto&& texture : gCloudResources.mTextures)
		{
			if (texture->mResource == nullptr)
				continue;

			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = texture->mFormat;
			srv_desc.ViewDimension = texture->mDepth == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE3D;
			srv_desc.Texture2D.MipLevels = static_cast<UINT>(-1);
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			gDevice->CreateShaderResourceView(texture->mResource.Get(), &srv_desc, handle);

			handle.ptr += increment_size;
		}
	}
}

void Scene::CleanupShaderResource()
{
	mOutputResource = nullptr;
	mDXRDescriptorHeap = nullptr;
}