#include "Scene.h"

#include "Common.h"
#include "PipelineState.h"
#include "ShaderTable.h"

#include "Atmosphere.h"
#include "Cloud.h"

#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "Thirdparty/tinyxml2/tinyxml2.h"
#include "Thirdparty/tinygltf/tiny_gltf.h"

Scene gScene;

void BLAS::Initialize(const InstanceInfo& inInstanceInfo, const InstanceData& inInstanceData, D3D12_GPU_VIRTUAL_ADDRESS inVertexBaseAddress, D3D12_GPU_VIRTUAL_ADDRESS inIndexBaseAddress)
{
	mDesc = {};
	mDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	mDesc.Triangles.VertexBuffer.StartAddress = inVertexBaseAddress + inInstanceData.mVertexOffset * sizeof(VertexType);
	mDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(VertexType);
	mDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	mDesc.Triangles.VertexCount = inInstanceData.mVertexCount;
	if (inInstanceData.mIndexCount > 0)
	{
		mDesc.Triangles.IndexBuffer = inIndexBaseAddress + inInstanceData.mIndexOffset * sizeof(IndexType);
		mDesc.Triangles.IndexCount = inInstanceData.mIndexCount;
		mDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
	}
	mDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	mInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	mInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	mInputs.NumDescs = 1;
	mInputs.pGeometryDescs = &mDesc;
	mInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	gDevice->GetRaytracingAccelerationStructurePrebuildInfo(&mInputs, &info);

	D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
	D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ScratchDataSizeInBytes);

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mScratch)));
	gSetName(mScratch, inInstanceInfo.mName, "[BLAS].Scratch");

	desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
	gSetName(mDest, inInstanceInfo.mName, "[BLAS].Dest");
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

	mBuilt = true;
}

void TLAS::Build(ID3D12GraphicsCommandList4* inCommandList)
{
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
	desc.Inputs = mInputs;
	desc.Inputs.InstanceDescs = mInstanceDescs->GetGPUVirtualAddress();
	desc.DestAccelerationStructureData = mDest->GetGPUVirtualAddress();
	desc.ScratchAccelerationStructureData = mScratch->GetGPUVirtualAddress();

	inCommandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
}

void TLAS::Initialize(const std::string& inName, const std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& inInstanceDescs)
{
	mInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	mInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	mInputs.NumDescs = static_cast<UINT>(inInstanceDescs.size());
	mInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	gDevice->GetRaytracingAccelerationStructurePrebuildInfo(&mInputs, &info);

	{
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ScratchDataSizeInBytes);
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mScratch)));
		gSetName(mScratch, inName, "[TLAS].Scratch");
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
		gSetName(mDest, inName, "[TLAS].Dest");
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * inInstanceDescs.size());
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mInstanceDescs)));
		gSetName(mInstanceDescs, inName, "[TLAS].InstanceDescs");

		uint8_t* pData = nullptr;
		mInstanceDescs->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, inInstanceDescs.data(), desc.Width);
		mInstanceDescs->Unmap(0, nullptr);
	}
}

bool Scene::LoadDummy(SceneContent& ioSceneContent)
{
	ioSceneContent = SceneContent();

	ioSceneContent.mIndices.push_back(0);
	ioSceneContent.mIndices.push_back(0);
	ioSceneContent.mIndices.push_back(0);

	ioSceneContent.mVertices.push_back({0, 0, 0});
	ioSceneContent.mNormals.push_back({0, 0, 0});
	ioSceneContent.mUVs.push_back({0, 0});
		
	InstanceInfo instance_info = {};
	InstanceData instance_data = {};
	FillDummyMaterial(instance_info, instance_data);

	instance_info.mName = "DummyInstance";
	ioSceneContent.mInstanceInfos.push_back(instance_info);

	instance_data.mTransform = glm::mat4(1);
	instance_data.mInverseTranspose = glm::mat4(1);
	instance_data.mVertexOffset = 0;
	instance_data.mVertexCount = 1;
	instance_data.mVertexOffset = 0;
	instance_data.mIndexCount = 3;
	ioSceneContent.mInstanceDatas.push_back(instance_data);

	return true; 
}

bool Scene::LoadObj(const std::string& inFilename, const glm::mat4x4& inTransform, SceneContent& ioSceneContent)
{	
	tinyobj::ObjReader reader;
	if (!reader.ParseFromFile(inFilename))
		return false;

	if (!reader.Warning().empty())
		gTrace(reader.Warning().c_str());

	if (!reader.Error().empty())
		gTrace(reader.Error().c_str());

	// Fetch indices, attributes
	glm::uint32 index = 0;
	for (auto&& shape : reader.GetShapes())
	{
		glm::uint32 index_offset = static_cast<glm::uint32>(ioSceneContent.mIndices.size());

		const int kNumFaceVerticesTriangle = 3;
		glm::uint32 index_count = static_cast<glm::uint32>(shape.mesh.num_face_vertices.size()) * kNumFaceVerticesTriangle;
		for (size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); face_index++)
		{
			assert(shape.mesh.num_face_vertices[face_index] == kNumFaceVerticesTriangle);
			for (size_t vertex_index = 0; vertex_index < kNumFaceVerticesTriangle; vertex_index++)
			{
				// [TODO] This makes index trivial...

				tinyobj::index_t idx = shape.mesh.indices[face_index * kNumFaceVerticesTriangle + vertex_index];
				ioSceneContent.mIndices.push_back(static_cast<IndexType>(index++));

				ioSceneContent.mVertices.push_back(VertexType(
					reader.GetAttrib().vertices[3 * idx.vertex_index + 0],
					reader.GetAttrib().vertices[3 * idx.vertex_index + 1],
					reader.GetAttrib().vertices[3 * idx.vertex_index + 2]
				));

				ioSceneContent.mNormals.push_back(NormalType(
					reader.GetAttrib().normals[3 * idx.normal_index + 0],
					reader.GetAttrib().normals[3 * idx.normal_index + 1],
					reader.GetAttrib().normals[3 * idx.normal_index + 2]
				));

				if (idx.texcoord_index == -1)
				{
					// Dummy uv if not available
					ioSceneContent.mUVs.push_back(UVType(0, 0));
				}
				else
				{
					ioSceneContent.mUVs.push_back(UVType(
						reader.GetAttrib().texcoords[2 * idx.texcoord_index + 0],
						reader.GetAttrib().texcoords[2 * idx.texcoord_index + 1]
					));
				}
			}
		}

		InstanceInfo instance_info = {};
		InstanceData instance_data = {};
		if (shape.mesh.material_ids.size() == 0 || shape.mesh.material_ids[0] == -1)
		{
			FillDummyMaterial(instance_info, instance_data);
		}
		else
		{
			const tinyobj::material_t& material = reader.GetMaterials()[shape.mesh.material_ids[0]];
			instance_info.mMaterialName = material.name;

			MaterialType type = MaterialType::Diffuse;
			switch (material.illum)
			{
			case 0: [[fallthrough]];
			case 1: type = MaterialType::Diffuse; break;
			case 2: type = MaterialType::RoughConductor; break;
			default: break;
			}
			instance_data.mMaterialType = type;
			instance_data.mTwoSided = false;
			instance_data.mAlbedo = glm::vec3(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
			instance_data.mOpacity = 1.0f;
			instance_data.mEmission = glm::vec3(material.emission[0], material.emission[1], material.emission[2]);
			instance_data.mReflectance = glm::vec3(material.specular[0], material.specular[1], material.specular[2]);
			instance_data.mRoughnessAlpha = material.roughness; // To match Mitsuba2 and PBRT (remaproughness = false)
			instance_data.mTransmittance = glm::vec3(material.transmittance[0], material.transmittance[1], material.transmittance[2]);
			instance_data.mIOR = glm::vec3(material.ior);
		}
		
		instance_info.mName = shape.name;
		ioSceneContent.mInstanceInfos.push_back(instance_info);
		
		instance_data.mTransform = inTransform;
		instance_data.mInverseTranspose = glm::transpose(glm::inverse(inTransform));
		instance_data.mVertexOffset = 0; // All vertices share same buffer. Only works indices fit in IndexType...
		instance_data.mVertexCount = index_count;
		instance_data.mIndexOffset = index_offset;
		instance_data.mIndexCount = index_count;
		ioSceneContent.mInstanceDatas.push_back(instance_data);
	}

	return true;
}

bool Scene::LoadMitsuba(const std::string& inFilename, SceneContent& ioSceneContent)
{
	static auto first_child_element_by_name_attribute = [](tinyxml2::XMLElement* inElement, const char* inName)
	{
		tinyxml2::XMLElement* child = inElement->FirstChildElement();
		while (child != nullptr)
		{
			std::string_view name = child->Attribute("name");
			if (name == inName)
				return child;

			child = child->NextSiblingElement();
		}

		return (tinyxml2::XMLElement*)nullptr;
	};

	static auto get_child_value = [](tinyxml2::XMLElement* inElement, const char* inName)
	{
		tinyxml2::XMLElement* child = first_child_element_by_name_attribute(inElement, inName);
		if (child == nullptr)
			return ""; // Treat no found the same as attribute with empty string to simplify parsing

		return child->Attribute("value");
	};

	tinyxml2::XMLDocument doc;
	doc.LoadFile(inFilename.c_str());

	tinyxml2::XMLElement* scene = doc.FirstChildElement("scene");

	typedef InstanceData MaterialData;
	std::unordered_map<std::string_view, MaterialData> materials_by_id;
	tinyxml2::XMLElement* bsdf = scene->FirstChildElement("bsdf");
	while (bsdf != nullptr)
	{
		std::string_view id = bsdf->Attribute("id");

		MaterialData material;

		tinyxml2::XMLElement* local_bsdf = bsdf;
		std::string_view local_type = bsdf->Attribute("type");
		if (local_type == "twosided")
		{
			local_bsdf = local_bsdf->FirstChildElement("bsdf");
			local_type = local_bsdf->Attribute("type");

			material.mTwoSided = true;
		}
		
		if (local_type == "diffuse")
		{
			material.mMaterialType = MaterialType::Diffuse;
			
			gVerify(std::sscanf(get_child_value(local_bsdf, "reflectance"), "%f, %f, %f", &material.mAlbedo.x, &material.mAlbedo.y, &material.mAlbedo.z) == 3);
		}
		else if (local_type == "roughconductor")
		{
			material.mMaterialType = MaterialType::RoughConductor;

			gAssert(std::string_view(get_child_value(local_bsdf, "distribution")) == "ggx"); // [TODO] Support beckmann?

			gVerify(std::sscanf(get_child_value(local_bsdf, "alpha"), 
				"%f", 
				&material.mRoughnessAlpha) == 1);

			gVerify(std::sscanf(get_child_value(local_bsdf, "specular_reflectance"), 
				"%f, %f, %f", 
				&material.mReflectance.x, &material.mReflectance.y, &material.mReflectance.z) == 3);
			
			gVerify(std::sscanf(get_child_value(local_bsdf, "eta"), 
				"%f, %f, %f", 
				&material.mEta.x, &material.mEta.y, &material.mEta.z) == 3);
			
			gVerify(std::sscanf(get_child_value(local_bsdf, "k"), 
				"%f, %f, %f", 
				&material.mK.x, &material.mK.y, &material.mK.z) == 3);
		}

		materials_by_id[id] = material;

		bsdf = bsdf->NextSiblingElement("bsdf");
	}

	tinyxml2::XMLElement* shape = scene->FirstChildElement("shape");
	while (shape != nullptr)
	{
		std::string_view type = shape->Attribute("type");
		std::string_view id = shape->Attribute("id");

		SceneContent* primitive = nullptr;
		if (type == "cube")
			primitive = &mPrimitives.mCube;
		else if (type == "rectangle")
			primitive = &mPrimitives.mRectangle;
		else if (type == "sphere")
			primitive = &mPrimitives.mSphere;

		if (primitive != nullptr)
		{
			glm::uint32 index_count = static_cast<glm::uint32>(primitive->mIndices.size());
			glm::uint32 vertex_offset = static_cast<glm::uint32>(ioSceneContent.mVertices.size());
			glm::uint32 index_offset = static_cast<glm::uint32>(ioSceneContent.mIndices.size());
			for (glm::uint32 i = 0; i < index_count; i++)
				ioSceneContent.mIndices.push_back(primitive->mIndices[i] + vertex_offset);

			std::copy(primitive->mVertices.begin(), primitive->mVertices.end(), std::back_inserter(ioSceneContent.mVertices));
			std::copy(primitive->mNormals.begin(), primitive->mNormals.end(), std::back_inserter(ioSceneContent.mNormals));
			std::copy(primitive->mUVs.begin(), primitive->mUVs.end(), std::back_inserter(ioSceneContent.mUVs));

			// Transform
			glm::mat4x4 matrix = glm::mat4x4(1.0f);
			{
				if (tinyxml2::XMLElement* transform = shape->FirstChildElement("transform"))
				{
					std::string_view transform_name = transform->Attribute("name");
					gAssert(transform_name == "to_world");

					std::string_view matrix_value = transform->FirstChildElement("matrix")->Attribute("value");
					gVerify(std::sscanf(matrix_value.data(),
						"%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
						&matrix[0][0], &matrix[1][0], &matrix[2][0], &matrix[3][0],
						&matrix[0][1], &matrix[1][1], &matrix[2][1], &matrix[3][1],
						&matrix[0][2], &matrix[1][2], &matrix[2][2], &matrix[3][2],
						&matrix[0][3], &matrix[1][3], &matrix[2][3], &matrix[3][3]) == 16);
				}
				else if (tinyxml2::XMLElement* center_element = shape->FirstChildElement("point"))
				{
					tinyxml2::XMLElement* radius_element = shape->FirstChildElement("float");
					gAssert(std::string_view(radius_element->Attribute("name")) == "radius");
					float radius = 0.0f;
					gVerify(std::sscanf(radius_element->Attribute("value"), "%f", &radius) == 1);

					gAssert(std::string_view(center_element->Attribute("name")) == "center");
					glm::vec3 center = glm::vec3(0.0f);
					gVerify(std::sscanf(center_element->Attribute("x"), "%f", &center[0]) == 1);
					gVerify(std::sscanf(center_element->Attribute("y"), "%f", &center[1]) == 1);
					gVerify(std::sscanf(center_element->Attribute("z"), "%f", &center[2]) == 1);

					matrix = glm::translate(matrix, center);
					matrix = glm::scale(matrix, glm::vec3(radius));
				}
			}

			InstanceInfo instance_info = {};
			InstanceData instance_data = {};

			// Material
			{
				bool found_material = false;
				tinyxml2::XMLElement* ref = shape->FirstChildElement("ref");
				if (ref != nullptr)
				{
					std::string_view material_id = ref->Attribute("id");
					auto iter = materials_by_id.find(material_id);
					if (iter != materials_by_id.end())
					{
						instance_info.mMaterialName = material_id;
						instance_data = iter->second;
						found_material = true;
					}
				}
				if (!found_material)
					FillDummyMaterial(instance_info, instance_data);
			}

			// Light
			{
				tinyxml2::XMLElement* emitter = shape->FirstChildElement("emitter");
				if (emitter != nullptr)
				{
					std::string_view emitter_type = emitter->Attribute("type");
					gAssert(emitter_type == "area");

					gVerify(std::sscanf(get_child_value(emitter, "radiance"),
						"%f, %f, %f",
						&instance_data.mEmission.x, &instance_data.mEmission.y, &instance_data.mEmission.z) == 3);

					// [Hack]
					instance_data.mTwoSided = false;

					Light light;
					light.mType = LightType::Count;
					if (primitive == &mPrimitives.mSphere)
						light.mType = LightType::Sphere;
					if (primitive == &mPrimitives.mRectangle)
						light.mType = LightType::Rectangle;
					light.mPosition = matrix[3];
					light.mRadius = matrix[0][0];
					ioSceneContent.mLights.push_back(light);
				}
			}

			instance_info.mName = id;
			ioSceneContent.mInstanceInfos.push_back(instance_info);

			instance_data.mTransform = matrix;
			instance_data.mInverseTranspose = glm::transpose(glm::inverse(matrix));
			instance_data.mVertexOffset = 0; // All vertices share same buffer. Only works indices fit in IndexType...
			instance_data.mVertexCount = index_count;
			instance_data.mIndexOffset = index_offset;
			instance_data.mIndexCount = index_count;
			ioSceneContent.mInstanceDatas.push_back(instance_data);
		}

		shape = shape->NextSiblingElement("shape");
	}

	tinyxml2::XMLElement* sensor = scene->FirstChildElement("sensor");
	if (sensor != nullptr)
	{
		tinyxml2::XMLElement* transform = first_child_element_by_name_attribute(sensor, "to_world");
		glm::mat4x4 matrix = glm::mat4x4(1.0f);
		gVerify(std::sscanf(transform->FirstChildElement("matrix")->Attribute("value"),
			"%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
			&matrix[0][0], &matrix[1][0], &matrix[2][0], &matrix[3][0],
			&matrix[0][1], &matrix[1][1], &matrix[2][1], &matrix[3][1],
			&matrix[0][2], &matrix[1][2], &matrix[2][2], &matrix[3][2],
			&matrix[0][3], &matrix[1][3], &matrix[2][3], &matrix[3][3]) == 16);		
		ioSceneContent.mCameraTransform = matrix;

		float fov = 90.0f;
		gVerify(std::sscanf(get_child_value(sensor, "fov"), "%f", &fov) == 1);
		ioSceneContent.mFov = fov;
	}

	ioSceneContent.mAtmosphereMode = AtmosphereMode::ConstantColor;

	return true;
}

void Scene::FillDummyMaterial(InstanceInfo& ioInstanceInfo, InstanceData& ioInstanceData)
{
	ioInstanceInfo.mMaterialName = "DummyMaterial";

	ioInstanceData.mMaterialType = MaterialType::Diffuse;
	ioInstanceData.mAlbedo = glm::vec3(0.18f, 0.18f, 0.18f);
	ioInstanceData.mOpacity = 1.0f;
	ioInstanceData.mEmission = glm::vec3(0.0f, 0.0f, 0.0f);
	ioInstanceData.mReflectance = glm::vec3(0.0f, 0.0f, 0.0f);
	ioInstanceData.mRoughnessAlpha = 1.0f;
	ioInstanceData.mTransmittance = glm::vec3(1.0f, 1.0f, 1.0f);
	ioInstanceData.mIOR = glm::vec3(1.0f, 1.0f, 1.0f);
}

void Scene::InitializeBuffers()
{
	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(0);
	D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

	gAssert(mSceneContent.mIndices.size() <= size_t(1) + std::numeric_limits<IndexType>::max());

	{
		desc.Width = sizeof(IndexType) * mSceneContent.mIndices.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mIndices)));
		gSetName(mBuffers.mIndices, "Scene", ".mIndexBuffer");

		uint8_t* pData = nullptr;
		mBuffers.mIndices->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mIndices.data(), desc.Width);
		mBuffers.mIndices->Unmap(0, nullptr);
	}

	{
		desc.Width =  sizeof(VertexType) * mSceneContent.mVertices.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mVertices)));
		gSetName(mBuffers.mVertices, "Scene", ".mBuffers.mVertices");

		uint8_t* pData = nullptr;
		mBuffers.mVertices->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mVertices.data(), desc.Width);
		mBuffers.mVertices->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(NormalType) * mSceneContent.mNormals.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mNormals)));
		gSetName(mBuffers.mNormals, "Scene", ".mBuffers.mNormals");

		uint8_t* pData = nullptr;
		mBuffers.mNormals->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mNormals.data(), desc.Width);
		mBuffers.mNormals->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(UVType) * mSceneContent.mUVs.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mUVs)));
		gSetName(mBuffers.mUVs, "Scene", ".mBuffers.mUVs");

		uint8_t* pData = nullptr;
		mBuffers.mUVs->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mUVs.data(), desc.Width);
		mBuffers.mUVs->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(InstanceData) * mSceneContent.mInstanceDatas.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mInstanceDatas)));
		gSetName(mBuffers.mInstanceDatas, "Scene", ".mBuffers.mInstanceDatas");

		uint8_t* pData = nullptr;
		mBuffers.mInstanceDatas->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mInstanceDatas.data(), desc.Width);
		mBuffers.mInstanceDatas->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(Light) * mSceneContent.mLights.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mLights)));
		gSetName(mBuffers.mLights, "Scene", ".mBuffers.mLights");

		uint8_t* pData = nullptr;
		mBuffers.mLights->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mLights.data(), desc.Width);
		mBuffers.mLights->Unmap(0, nullptr);
	}
}

void Scene::InitializeAccelerationStructures()
{
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instance_descs;
	instance_descs.resize(GetInstanceCount());
	for (int instance_index = 0; instance_index < GetInstanceCount(); instance_index++)
	{
		const InstanceInfo& instance_info = GetInstanceInfo(instance_index);
		const InstanceData& instance_data = GetInstanceData(instance_index);

		BLASRef blas = std::make_shared<BLAS>();
		blas->Initialize(instance_info, instance_data, mBuffers.mVertices->GetGPUVirtualAddress(), mBuffers.mIndices->GetGPUVirtualAddress());
		mBlases.push_back(blas);

		glm::mat4x4 transform = glm::transpose(instance_data.mTransform); // column-major -> row-major
		memcpy(instance_descs[instance_index].Transform, &transform, sizeof(instance_descs[instance_index].Transform));
		instance_descs[instance_index].InstanceID = instance_index; // This value will be exposed to the shader via InstanceID()
		instance_descs[instance_index].InstanceMask = 0xFF;
		instance_descs[instance_index].InstanceContributionToHitGroupIndex = 0; // [TODO]
		instance_descs[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance_descs[instance_index].AccelerationStructure = blas->GetGPUVirtualAddress();
	}

	mTLAS = std::make_shared<TLAS>();
	mTLAS->Initialize("Default", instance_descs);
}

void Scene::Load(const char* inFilename, const glm::mat4x4& inTransform)
{
	LoadObj("Asset/primitives/cube.obj", glm::mat4x4(1.0f), mPrimitives.mCube);
	LoadObj("Asset/primitives/rectangle.obj", glm::mat4x4(1.0f), mPrimitives.mRectangle);
	LoadObj("Asset/primitives/sphere.obj", glm::mat4x4(1.0f), mPrimitives.mSphere);

	mSceneContent = {}; // Reset

	std::string filename_lower;
	if (inFilename != nullptr)
		filename_lower = inFilename;
	filename_lower = gToLower(filename_lower);
	
	bool loaded = false;
	bool try_load = std::filesystem::exists(filename_lower); 
	
	if (!loaded && try_load && filename_lower.ends_with(".obj"))
		loaded |= LoadObj(filename_lower, inTransform, mSceneContent);

	if (!loaded && try_load && filename_lower.ends_with(".xml"))
		loaded |= LoadMitsuba(filename_lower, mSceneContent);
	
	if (mSceneContent.mInstanceDatas.empty())
		loaded |= LoadDummy(mSceneContent);

	assert(loaded);

	if (mSceneContent.mLights.empty())
		mSceneContent.mLights.push_back({});

	InitializeBuffers();
	InitializeAccelerationStructures();

	gCreatePipelineState();
	CreateShaderResource();
	gCreateShaderTable();
}

void Scene::Unload()
{
	gCleanupShaderTable();
	gCleanupPipelineState();

	mPrimitives = {};
	mSceneContent = {};
	mBlases = {};
	mTLAS = {};
	mBuffers = {};
}

void Scene::Build(ID3D12GraphicsCommandList4* inCommandList)
{
	for (auto&& blas : mBlases)
		blas->Build(inCommandList);

	gBarrierUAV(inCommandList, nullptr);

	mTLAS->Build(inCommandList);

	gBarrierUAV(inCommandList, nullptr);
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
	auto create_acceleration_structure_SRV = [](ID3D12Resource* inResource, ViewDescriptorIndex inViewDescriptorIndex)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		desc.RaytracingAccelerationStructure.Location = inResource->GetGPUVirtualAddress();

		for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
			gDevice->CreateShaderResourceView(nullptr, &desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(inViewDescriptorIndex));
	};
	create_acceleration_structure_SRV(mTLAS->GetResource(), ViewDescriptorIndex::RaytraceTLASSRV);

	auto create_buffer_SRV = [](ID3D12Resource* inResource, int inStride, ViewDescriptorIndex inViewDescriptorIndex)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};		
		D3D12_RESOURCE_DESC resource_desc = inResource->GetDesc();
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		desc.Buffer.NumElements = static_cast<UINT>(resource_desc.Width / inStride);
		desc.Buffer.StructureByteStride = inStride;
		desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
			gDevice->CreateShaderResourceView(inResource, &desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(inViewDescriptorIndex));
	};
	create_buffer_SRV(mBuffers.mInstanceDatas.Get(),	sizeof(InstanceData),	ViewDescriptorIndex::RaytraceInstanceDataSRV);
	create_buffer_SRV(mBuffers.mIndices.Get(),			sizeof(IndexType),		ViewDescriptorIndex::RaytraceIndicesSRV);
	create_buffer_SRV(mBuffers.mVertices.Get(),			sizeof(VertexType),		ViewDescriptorIndex::RaytraceVerticesSRV);
	create_buffer_SRV(mBuffers.mNormals.Get(),			sizeof(NormalType),		ViewDescriptorIndex::RaytraceNormalsSRV);
	create_buffer_SRV(mBuffers.mUVs.Get(),				sizeof(UVType),			ViewDescriptorIndex::RaytraceUVsSRV);
	create_buffer_SRV(mBuffers.mLights.Get(),			sizeof(Light),			ViewDescriptorIndex::RaytraceLightsSRV);
}