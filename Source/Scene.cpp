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
	mInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	mInputs.NumDescs = 1;
	mInputs.pGeometryDescs = &mDesc;
	mInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	gDevice->GetRaytracingAccelerationStructurePrebuildInfo(&mInputs, &info);

	D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
	D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ScratchDataSizeInBytes);

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mScratch)));
	gSetName(mScratch, mName, ".BLAS.Scratch");

	desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
	gSetName(mDest, mName, ".BLAS.Dest");
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
		gSetName(mScratch, mName, ".TLAS.Scratch");
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
		gSetName(mDest, mName, ".TLAS.Dest");
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mObjectInstances.size());
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mInstanceDescs)));
		gSetName(mInstanceDescs, mName, ".TLAS.InstanceDescs");

		// Map once
		mInstanceDescs->Map(0, nullptr, reinterpret_cast<void**>(&mInstanceDescsPointer));
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(sizeof(InstanceData) * mObjectInstances.size());
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mInstanceBuffer)));
		gSetName(mInstanceBuffer, mName, ".TLAS.InstanceBuffer");

		// Map once
		mInstanceBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mInstanceBufferPointer));
	}
}

void TLAS::UpdateObjectInstances()
{
	int instance_index = 0;
	for (auto&& object_instance : mObjectInstances)
	{
		glm::mat4x4 transform = glm::transpose(object_instance->Transform());
		memcpy(mInstanceDescsPointer[instance_index].Transform, &transform, sizeof(mInstanceDescsPointer[instance_index].Transform));

		mInstanceDescsPointer[instance_index].InstanceID = instance_index; // This value will be exposed to the shader via InstanceID()
		mInstanceDescsPointer[instance_index].InstanceMask = 0xFF;
		mInstanceDescsPointer[instance_index].InstanceContributionToHitGroupIndex = object_instance->GetHitGroupIndex(); // Match shader table
		mInstanceDescsPointer[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		mInstanceDescsPointer[instance_index].AccelerationStructure = object_instance->GetBLAS()->GetGPUVirtualAddress();

		mInstanceBufferPointer[instance_index] = object_instance->mInstanceData;

		instance_index++;
	}
}

bool Scene::LoadDummy(SceneContent& ioCollection)
{
	ioCollection = SceneContent();

	ioCollection.mIndices.push_back(0);
	ioCollection.mIndices.push_back(0);
	ioCollection.mIndices.push_back(0);

	ioCollection.mVertices.push_back(VertexType(0, 0, 0));
	ioCollection.mNormals.push_back(NormalType(0, 0, 0));
	ioCollection.mUVs.push_back(UVType(0, 0));

	BLASRef blas = std::make_shared<BLAS>(std::make_shared<Primitive>(0, 1, 0, 3), "Dummy");
	ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, glm::mat4(1), kDefaultHitGroupIndex);
	ioCollection.mObjectInstances.push_back(object_instance);

	return true; 
}

bool Scene::LoadObj(const std::string& inFilename, const glm::mat4x4& inTransform, Scene::SceneContent& ioCollection)
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
		glm::uint32 index_offset = static_cast<glm::uint32>(ioCollection.mIndices.size());

		const int kNumFaceVerticesTriangle = 3;
		glm::uint32 index_count = static_cast<glm::uint32>(shape.mesh.num_face_vertices.size()) * kNumFaceVerticesTriangle;
		for (size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); face_index++)
		{
			assert(shape.mesh.num_face_vertices[face_index] == kNumFaceVerticesTriangle);
			for (size_t vertex_index = 0; vertex_index < kNumFaceVerticesTriangle; vertex_index++)
			{
				// [TODO] This makes index trivial...

				tinyobj::index_t idx = shape.mesh.indices[face_index * kNumFaceVerticesTriangle + vertex_index];
				ioCollection.mIndices.push_back(static_cast<IndexType>(index++));

				ioCollection.mVertices.push_back(VertexType(
					reader.GetAttrib().vertices[3 * idx.vertex_index + 0],
					reader.GetAttrib().vertices[3 * idx.vertex_index + 1],
					reader.GetAttrib().vertices[3 * idx.vertex_index + 2]
				));

				ioCollection.mNormals.push_back(NormalType(
					reader.GetAttrib().normals[3 * idx.normal_index + 0],
					reader.GetAttrib().normals[3 * idx.normal_index + 1],
					reader.GetAttrib().normals[3 * idx.normal_index + 2]
				));

				if (idx.texcoord_index == -1)
				{
					// Dummy uv if not available
					ioCollection.mUVs.push_back(UVType(0, 0));
				}
				else
				{
					ioCollection.mUVs.push_back(UVType(
						reader.GetAttrib().texcoords[2 * idx.texcoord_index + 0],
						reader.GetAttrib().texcoords[2 * idx.texcoord_index + 1]
					));
				}
			}
		}

		glm::uint32 vertex_offset = 0; // All vertices share same buffer. Only works indices fit in IndexType
		glm::uint32 vertex_count = index_count;

		BLASRef blas = std::make_shared<BLAS>(std::make_shared<Primitive>(vertex_offset, vertex_count, index_offset, index_count), shape.name);
		ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, inTransform, kDefaultHitGroupIndex);
		object_instance->mName = shape.name;
		ioCollection.mObjectInstances.push_back(object_instance);

		if (shape.mesh.material_ids.size() == 0 || shape.mesh.material_ids[0] == -1)
		{
			FillDummyMaterial(object_instance->mInstanceData);
		}
		else
		{
			const tinyobj::material_t& material = reader.GetMaterials()[shape.mesh.material_ids[0]];
			object_instance->mMaterialName = material.name;

			MaterialType type = MaterialType::Diffuse;
			switch (material.illum)
			{
			case 0: [[fallthrough]];
			case 1: type = MaterialType::Diffuse; break;
			case 2: type = MaterialType::RoughConductor; break;
			default: break;
			}
			object_instance->mInstanceData.mMaterialType = type;
			object_instance->mInstanceData.mAlbedo = glm::vec3(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
			object_instance->mInstanceData.mOpacity = 1.0f;
			object_instance->mInstanceData.mEmission = glm::vec3(material.emission[0], material.emission[1], material.emission[2]);
			object_instance->mInstanceData.mReflectance = glm::vec3(material.specular[0], material.specular[1], material.specular[2]);
			object_instance->mInstanceData.mRoughnessAlpha = material.roughness; // To match Mitsuba2 and PBRT (remaproughness = false)
			object_instance->mInstanceData.mTransmittance = glm::vec3(material.transmittance[0], material.transmittance[1], material.transmittance[2]);
			object_instance->mInstanceData.mIOR = glm::vec3(material.ior);
		}

		object_instance->mInstanceData.mTransform = inTransform;
		object_instance->mInstanceData.mInverseTranspose = glm::transpose(glm::inverse(inTransform));
		object_instance->mInstanceData.mIndexOffset = index_offset;
		object_instance->mInstanceData.mVertexOffset = vertex_offset;
	}

	return true;
}

bool Scene::LoadMitsuba(const std::string& inFilename, SceneContent& ioCollection)
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

	std::unordered_map<std::string_view, InstanceData> materials_by_id;
	tinyxml2::XMLElement* bsdf = scene->FirstChildElement("bsdf");
	while (bsdf != nullptr)
	{
		std::string_view id = bsdf->Attribute("id");

		tinyxml2::XMLElement* local_bsdf = bsdf;
		std::string_view local_type = bsdf->Attribute("type");
		if (local_type == "twosided")
		{
			local_bsdf = local_bsdf->FirstChildElement("bsdf");
			local_type = local_bsdf->Attribute("type");
		}

		InstanceData material;
		if (local_type == "diffuse")
		{
			material.mMaterialType = MaterialType::Diffuse;
			
			gVerify(std::sscanf(get_child_value(local_bsdf, "reflectance"), "%f, %f, %f", &material.mAlbedo.x, &material.mAlbedo.y, &material.mAlbedo.z) == 3);
		}
		else if (local_type == "roughconductor")
		{
			material.mMaterialType = MaterialType::RoughConductor;

			gAssert(std::string_view(get_child_value(local_bsdf, "distribution")) == "ggx"); // [TODO] Support beckmann?

			gVerify(std::sscanf(get_child_value(local_bsdf, "alpha"), "%f", &material.mRoughnessAlpha) == 1);

			// [Mitsuba3] From fresnel_conductor in fresnel.h
			// See also https://seblagarde.wordpress.com/2013/04/29/memo-on-fresnel-equations/
			// See also "Optics" book
			auto fresnel_conductor = [](glm::vec3 inEta, glm::vec3 inK, float inCosTheta)
			{
				// Modified from "Optics" by K.D. Moeller, University Science Books, 1988
				float cos_theta_i_2 = inCosTheta * inCosTheta,
					sin_theta_i_2 = 1.f - cos_theta_i_2,
					sin_theta_i_4 = sin_theta_i_2 * sin_theta_i_2;

				auto eta_r = inEta,
					eta_i = inK;

				glm::vec3 temp_1 = eta_r * eta_r - eta_i * eta_i - sin_theta_i_2,
					a_2_pb_2 = glm::sqrt(temp_1 * temp_1 + 4.f * eta_i * eta_i * eta_r * eta_r),
					a = glm::sqrt(.5f * (a_2_pb_2 + temp_1));

				glm::vec3 term_1 = a_2_pb_2 + cos_theta_i_2,
					term_2 = 2.f * inCosTheta * a;

				glm::vec3 r_s = (term_1 - term_2) / (term_1 + term_2);

				glm::vec3 term_3 = a_2_pb_2 * cos_theta_i_2 + sin_theta_i_4,
					term_4 = term_2 * sin_theta_i_2;

				glm::vec3 r_p = r_s * (term_3 - term_4) / (term_3 + term_4);

				return 0.5f * (r_s + r_p);
			};

			glm::vec3 specular_reflectance;
			gVerify(std::sscanf(get_child_value(local_bsdf, "specular_reflectance"), 
				"%f, %f, %f", 
				&specular_reflectance.x, &specular_reflectance.y, &specular_reflectance.z) == 3);
			glm::vec3 eta;
			gVerify(std::sscanf(get_child_value(local_bsdf, "eta"), 
				"%f, %f, %f", 
				&eta.x, &eta.y, &eta.z) == 3);
			glm::vec3 k;
			gVerify(std::sscanf(get_child_value(local_bsdf, "k"), 
				"%f, %f, %f", 
				&k.x, &k.y, &k.z) == 3);
			material.mReflectance = specular_reflectance * fresnel_conductor(eta, k, 1); // Use F0 for now
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
			primitive = &mPrimitiveObjectCollection.mCube;
		else if (type == "rectangle")
			primitive = &mPrimitiveObjectCollection.mRectangle;
		else if (type == "sphere")
			primitive = &mPrimitiveObjectCollection.mSphere;

		if (primitive != nullptr)
		{
			glm::uint32 index_count = static_cast<glm::uint32>(primitive->mIndices.size());

			glm::uint32 vertex_offset = static_cast<glm::uint32>(ioCollection.mVertices.size());
			glm::uint32 index_offset = static_cast<glm::uint32>(ioCollection.mIndices.size());
			for (glm::uint32 i = 0; i < index_count; i++)
				ioCollection.mIndices.push_back(primitive->mIndices[i] + vertex_offset);

			std::copy(primitive->mVertices.begin(), primitive->mVertices.end(), std::back_inserter(ioCollection.mVertices));
			std::copy(primitive->mNormals.begin(), primitive->mNormals.end(), std::back_inserter(ioCollection.mNormals));
			std::copy(primitive->mUVs.begin(), primitive->mUVs.end(), std::back_inserter(ioCollection.mUVs));

			vertex_offset = 0; // All vertices share same buffer. Only works when indices fit in IndexType
			glm::uint32 vertex_count = index_count;
			BLASRef blas = std::make_shared<BLAS>(std::make_shared<Primitive>(vertex_offset, vertex_count, index_offset, index_count), id.data());

			glm::mat4x4 matrix = glm::mat4x4(1.0f);
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

			ObjectInstanceRef object_instance = std::make_shared<ObjectInstance>(blas, matrix, kDefaultHitGroupIndex);
			object_instance->mName = id;
			ioCollection.mObjectInstances.push_back(object_instance);

			bool found_material = false;
			tinyxml2::XMLElement* ref = shape->FirstChildElement("ref");
			if (ref != nullptr)
			{
				std::string_view material_id = ref->Attribute("id");
				auto iter = materials_by_id.find(material_id);
				if (iter != materials_by_id.end())
				{
					object_instance->mMaterialName = material_id;

					InstanceData& material = iter->second;
					object_instance->mInstanceData.mMaterialType = material.mMaterialType;
					object_instance->mInstanceData.mAlbedo = material.mAlbedo;
					object_instance->mInstanceData.mOpacity = material.mOpacity;
					object_instance->mInstanceData.mEmission = material.mEmission;
					object_instance->mInstanceData.mReflectance = material.mReflectance;
					object_instance->mInstanceData.mRoughnessAlpha = material.mRoughnessAlpha;
					object_instance->mInstanceData.mTransmittance = material.mTransmittance;
					object_instance->mInstanceData.mIOR = material.mIOR;

					found_material = true;
				}
			}
			
			if (!found_material)
				FillDummyMaterial(object_instance->mInstanceData);

			tinyxml2::XMLElement* emitter = shape->FirstChildElement("emitter");
			if (emitter != nullptr)
			{
				std::string_view emitter_type = emitter->Attribute("type");
				gAssert(emitter_type == "area");

				InstanceData& material = object_instance->mInstanceData;
				gVerify(std::sscanf(get_child_value(emitter, "radiance"), 
					"%f, %f, %f", 
					&material.mEmission.x, &material.mEmission.y, &material.mEmission.z) == 3);
			}

			object_instance->mInstanceData.mTransform = matrix;
			object_instance->mInstanceData.mInverseTranspose = glm::transpose(glm::inverse(matrix));
			object_instance->mInstanceData.mIndexOffset = index_offset;
			object_instance->mInstanceData.mVertexOffset = vertex_offset;
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
		ioCollection.mCameraTransform = matrix;

		float fov = 90.0f;
		gVerify(std::sscanf(get_child_value(sensor, "fov"), "%f", &fov) == 1);
		ioCollection.mFov = fov;
	}

	ioCollection.mAtmosphereMode = AtmosphereMode::ConstantColor;

	return true;
}

void Scene::FillDummyMaterial(InstanceData& ioInstanceData)
{
	ioInstanceData.mMaterialType = MaterialType::Diffuse;
	ioInstanceData.mAlbedo = glm::vec3(0.18f, 0.18f, 0.18f);
	ioInstanceData.mOpacity = 1.0f;
	ioInstanceData.mEmission = glm::vec3(0.0f, 0.0f, 0.0f);
	ioInstanceData.mReflectance = glm::vec3(0.0f, 0.0f, 0.0f);
	ioInstanceData.mRoughnessAlpha = 1.0f;
	ioInstanceData.mTransmittance = glm::vec3(1.0f, 1.0f, 1.0f);
	ioInstanceData.mIOR = glm::vec3(1.0f, 1.0f, 1.0f);
}

void Scene::InitializeAS(Scene::SceneContent& inContext)
{
	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(0);
	D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

	gAssert(inContext.mIndices.size() <= size_t(1) + std::numeric_limits<IndexType>::max());

	{
		desc.Width = sizeof(IndexType) * inContext.mIndices.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mIndexBuffer)));
		gSetName(mIndexBuffer, "Scene", ".IndexBuffer");

		uint8_t* pData = nullptr;
		mIndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, inContext.mIndices.data(), desc.Width);
		mIndexBuffer->Unmap(0, nullptr);
	}

	{
		desc.Width =  sizeof(VertexType) * inContext.mVertices.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mVertexBuffer)));
		gSetName(mVertexBuffer, "Scene", ".VertexBuffer");

		uint8_t* pData = nullptr;
		mVertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, inContext.mVertices.data(), desc.Width);
		mVertexBuffer->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(NormalType) * inContext.mNormals.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mNormalBuffer)));
		gSetName(mNormalBuffer, "Scene", ".NormalBuffer");

		uint8_t* pData = nullptr;
		mNormalBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, inContext.mNormals.data(), desc.Width);
		mNormalBuffer->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(UVType) * inContext.mUVs.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mUVBuffer)));
		gSetName(mUVBuffer, "Scene", ".UVBuffer");

		uint8_t* pData = nullptr;
		mUVBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, inContext.mUVs.data(), desc.Width);
		mUVBuffer->Unmap(0, nullptr);
	}

	for (auto&& object_instance : inContext.mObjectInstances)
		object_instance->GetBLAS()->Initialize(mVertexBuffer->GetGPUVirtualAddress(), mIndexBuffer->GetGPUVirtualAddress());

	mTLAS = std::make_shared<TLAS>("Scene");
	mTLAS->Initialize(std::move(inContext.mObjectInstances));
}

void Scene::Load(const char* inFilename, const glm::mat4x4& inTransform)
{
	LoadObj("Asset/primitives/cube.obj", glm::mat4x4(1.0f), mPrimitiveObjectCollection.mCube);
	LoadObj("Asset/primitives/rectangle.obj", glm::mat4x4(1.0f), mPrimitiveObjectCollection.mRectangle);
	LoadObj("Asset/primitives/sphere.obj", glm::mat4x4(1.0f), mPrimitiveObjectCollection.mSphere);

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
	
	if (mSceneContent.mObjectInstances.empty())
		loaded |= LoadDummy(mSceneContent);

	assert(loaded);
	InitializeAS(mSceneContent);

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

	mPrimitiveObjectCollection = PrimitiveObjectCollection();
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

		// t5
		{
			D3D12_RESOURCE_DESC resource_desc = mUVBuffer->GetDesc();
			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			desc.Buffer.NumElements = static_cast<UINT>(resource_desc.Width / sizeof(Scene::UVType));
			desc.Buffer.StructureByteStride = sizeof(Scene::UVType);
			gDevice->CreateShaderResourceView(mUVBuffer.Get(), &desc, handle);

			handle.ptr += increment_size;
		}

		// b, space2 - Atmosphere
		if (gAtmosphere.mResource.mConstantUploadBuffer != nullptr)
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			desc.BufferLocation = gAtmosphere.mResource.mConstantUploadBuffer->GetGPUVirtualAddress();
			desc.SizeInBytes = gAlignUp(static_cast<UINT>(sizeof(AtmosphereConstants)), static_cast<UINT>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			gDevice->CreateConstantBufferView(&desc, handle);

			handle.ptr += increment_size;
		}

		// t, space2 - Atmosphere
		for (auto&& texture : gAtmosphere.mResource.mTextures)
		{
			if (texture.mResource == nullptr)
				continue;

			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = texture.mFormat;
			srv_desc.ViewDimension = texture.mDepth == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE3D;
			srv_desc.Texture2D.MipLevels = static_cast<UINT>(-1);
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			gDevice->CreateShaderResourceView(texture.mResource.Get(), &srv_desc, handle);

			handle.ptr += increment_size;
		}

		// b, space3 - Cloud
		if (gCloud.mResource.mConstantUploadBuffer != nullptr)
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			desc.BufferLocation = gCloud.mResource.mConstantUploadBuffer->GetGPUVirtualAddress();
			desc.SizeInBytes = gAlignUp(static_cast<UINT>(sizeof(Cloud)), static_cast<UINT>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			gDevice->CreateConstantBufferView(&desc, handle);

			handle.ptr += increment_size;
		}

		// t, space3 - Cloud
		for (auto&& texture : gCloud.mResource.mTextures)
		{
			if (texture.mResource == nullptr)
				continue;

			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.Format = texture.mFormat;
			srv_desc.ViewDimension = texture.mDepth == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE3D;
			srv_desc.Texture2D.MipLevels = static_cast<UINT>(-1);
			srv_desc.Texture2D.MostDetailedMip = 0;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			gDevice->CreateShaderResourceView(texture.mResource.Get(), &srv_desc, handle);

			handle.ptr += increment_size;
		}
	}
}

void Scene::CleanupShaderResource()
{
	mOutputResource = nullptr;
	mDXRDescriptorHeap = nullptr;
}