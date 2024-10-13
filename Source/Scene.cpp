#include "Scene.h"

#include "Common.h"

#include "Atmosphere.h"
#include "Cloud.h"

#include "Thirdparty/glm/glm/gtx/matrix_decompose.hpp"
#include "Thirdparty/tinyxml2/tinyxml2.h"
#include "Thirdparty/tinygltf/stb_image.h"
#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "Thirdparty/tiny_gltf.h"

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
	mDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

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
	gSetName(mScratch, "Scene.", inInstanceInfo.mName, ".[BLAS].Scratch");

	desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
	gSetName(mDest, "Scene.", inInstanceInfo.mName, ".[BLAS].Dest");
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
		gSetName(mScratch, "Scene", inName, ".[TLAS].Scratch");
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
		gSetName(mDest, "Scene", inName, ".[TLAS].Dest");
	}

	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * inInstanceDescs.size());
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mInstanceDescs)));
		gSetName(mInstanceDescs, "Scene", inName, ".[TLAS].InstanceDescs");

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

	ioSceneContent.mVertices.push_back({ 0, 0, 0 });
	ioSceneContent.mNormals.push_back({ 0, 0, 0 });
	ioSceneContent.mUVs.push_back({ 0, 0 });

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

bool Scene::LoadObj(const std::string& inFilename, const glm::mat4x4& inTransform, bool inFlipV, SceneContent& ioSceneContent)
{
	tinyobj::ObjReader reader;
	if (!reader.ParseFromFile(inFilename))
		return false;

	if (!reader.Warning().empty())
		gTrace(reader.Warning().c_str());

	if (!reader.Error().empty())
		gTrace(reader.Error().c_str());

	// Fetch indices, attributes
	uint32_t index = 0;
	for (auto&& shape : reader.GetShapes())
	{
		uint32_t vertex_offset = static_cast<uint32_t>(ioSceneContent.mVertices.size());
		uint32_t index_offset = static_cast<uint32_t>(ioSceneContent.mIndices.size());

		const int kVertexCountPerTriangle = 3;
		uint32_t vertex_count = static_cast<uint32_t>(shape.mesh.num_face_vertices.size()) * kVertexCountPerTriangle;
		uint32_t index_count = static_cast<uint32_t>(shape.mesh.num_face_vertices.size()) * kVertexCountPerTriangle;

		for (size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); face_index++)
		{
			assert(shape.mesh.num_face_vertices[face_index] == kVertexCountPerTriangle);
			for (size_t vertex_index = 0; vertex_index < kVertexCountPerTriangle; vertex_index++)
			{
				tinyobj::index_t idx = shape.mesh.indices[face_index * kVertexCountPerTriangle + vertex_index];
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
						inFlipV ? 1.0f - reader.GetAttrib().texcoords[2 * idx.texcoord_index + 1] : reader.GetAttrib().texcoords[2 * idx.texcoord_index + 1]
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

			BSDF type = BSDF::Unsupported;
			switch (material.illum)
			{
			case 0: [[fallthrough]];
			case 1: type = BSDF::Diffuse; break;
			case 2: type = BSDF::RoughConductor; break;
			default: break;
			}
			instance_data.mBSDF = type;
			instance_data.mTwoSided = false;
			instance_data.mAlbedo = glm::vec3(material.diffuse[0], material.diffuse[1], material.diffuse[2]);
			instance_data.mOpacity = 1.0f;
			instance_data.mEmission = glm::vec3(material.emission[0], material.emission[1], material.emission[2]);
			instance_data.mRoughnessAlpha = material.roughness; // To match Mitsuba2 and PBRT (remaproughness = false)
			instance_data.mSpecularReflectance = glm::vec3(material.specular[0], material.specular[1], material.specular[2]);
			instance_data.mSpecularTransmittance = glm::vec3(material.transmittance[0], material.transmittance[1], material.transmittance[2]);
		}

		instance_info.mName = shape.name;
		ioSceneContent.mInstanceInfos.push_back(instance_info);

		instance_data.mTransform = inTransform;
		instance_data.mInverseTranspose = glm::transpose(glm::inverse(inTransform));
		instance_data.mVertexOffset = vertex_offset;
		instance_data.mVertexCount = vertex_count;
		instance_data.mIndexOffset = index_offset;
		instance_data.mIndexCount = index_count;
		ioSceneContent.mInstanceDatas.push_back(instance_data);
	}

	return true;
}

bool Scene::LoadMitsuba(const std::string& inFilename, SceneContent& ioSceneContent)
{
	static auto get_first_child_element_by_name = [](tinyxml2::XMLElement* inElement, const char* inName)
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
			tinyxml2::XMLElement* child = get_first_child_element_by_name(inElement, inName);
			if (child == nullptr)
				return std::string_view(); // Treat no found the same as attribute with empty string to simplify parsing

			return std::string_view(child->Attribute("value"));
		};

	static auto get_child_texture = [](tinyxml2::XMLElement* inElement, const char* inName)
		{
			tinyxml2::XMLElement* child = get_first_child_element_by_name(inElement, inName);
			if (child == nullptr)
				return std::string_view();

			return get_child_value(child, "filename");
		};

	static auto null_to_empty = [](const char* str)
		{
			if (str == nullptr)
				return "";

			return str;
		};

	tinyxml2::XMLDocument doc;
	doc.LoadFile(inFilename.c_str());

	tinyxml2::XMLElement* scene = doc.FirstChildElement("scene");

	struct BSDFInstance
	{
		InstanceInfo mInstanceInfo;
		InstanceData mInstanceData;
	};

	std::unordered_map<std::string_view, BSDFInstance> bsdf_instance_by_id;
	tinyxml2::XMLElement* bsdf = scene->FirstChildElement("bsdf");
	while (bsdf != nullptr)
	{
		BSDFInstance bsdf_instance;

		const char* id = bsdf->Attribute("id");

		tinyxml2::XMLElement* local_bsdf = bsdf;
		std::string_view local_type = bsdf->Attribute("type");

		auto process_twosided = [&]()
			{
				if (local_type == "twosided")
				{
					bsdf_instance.mInstanceData.mTwoSided = true;

					local_bsdf = local_bsdf->FirstChildElement("bsdf");
					local_type = local_bsdf->Attribute("type");

					return true;
				}

				return false;
			};

		auto process_mask = [&]()
			{
				if (local_type == "mask")
				{
					std::string_view opacity = get_child_value(local_bsdf, "opacity");
					gFromString(opacity.data(), bsdf_instance.mInstanceData.mOpacity);

					// Could be texture

					local_bsdf = local_bsdf->FirstChildElement("bsdf");
					local_type = local_bsdf->Attribute("type");

					return true;
				}

				return false;
			};

		auto process_bumpmap = [&]()
			{
				if (local_type == "bumpmap")
				{
					std::filesystem::path path = inFilename;
					path.replace_filename(std::filesystem::path(get_child_texture(local_bsdf, "map")));
					bsdf_instance.mInstanceInfo.mNormalTexture = path;

					local_bsdf = local_bsdf->FirstChildElement("bsdf");
					local_type = local_bsdf->Attribute("type");

					gAssert(id == nullptr);
					id = local_bsdf->Attribute("id");

					return true;
				}

				return false;
			};

		while (true)
		{
			// Process adapters
			bool changed = false;
			changed |= process_twosided();
			changed |= process_mask();
			changed |= process_bumpmap();
			if (!changed)
				break;
		}

		auto load_diffuse_reflectance = [&]() { gFromString(get_child_value(local_bsdf, "reflectance").data(), bsdf_instance.mInstanceData.mAlbedo); };
		auto load_specular_reflectance = [&]() { gFromString(get_child_value(local_bsdf, "specular_reflectance").data(), bsdf_instance.mInstanceData.mSpecularReflectance); };
		auto load_specular_transmittance = [&]() { gFromString(get_child_value(local_bsdf, "specular_transmittance").data(), bsdf_instance.mInstanceData.mSpecularTransmittance); };
		auto load_eta = [&]() { gFromString(get_child_value(local_bsdf, "eta").data(), bsdf_instance.mInstanceData.mEta); };
		auto load_k = [&]() { gFromString(get_child_value(local_bsdf, "k").data(), bsdf_instance.mInstanceData.mK); };
		// [TODO] Support anisotropy
		auto load_alpha = [&]() { gFromString(get_child_value(local_bsdf, "alpha").data(), bsdf_instance.mInstanceData.mRoughnessAlpha); };

		if (local_type == "diffuse")
		{
			bsdf_instance.mInstanceData.mBSDF = BSDF::Diffuse;

			std::string_view reflectance = get_child_texture(local_bsdf, "reflectance");

			std::filesystem::path path = inFilename;
			path.replace_filename(std::filesystem::path(reflectance));

			if (reflectance.empty())
				load_diffuse_reflectance();
			else
			{
				bsdf_instance.mInstanceInfo.mAlbedoTexture = path;
				bsdf_instance.mInstanceData.mAlbedo = glm::vec3(1.0f);
			}
		}
		else if (local_type == "roughconductor")
		{
			bsdf_instance.mInstanceData.mBSDF = BSDF::RoughConductor;

			// [TODO] Support beckmann
			// [TODO] Support sample_visible
			gAssert(std::string_view(get_child_value(local_bsdf, "distribution")) == "ggx");
			load_alpha();

			load_eta();
			load_k();

			load_specular_reflectance();
		}
		else if (local_type == "dielectric")
		{
			bsdf_instance.mInstanceData.mBSDF = BSDF::Dielectric;

			float int_ior = 1.0f;
			gFromString(get_child_value(local_bsdf, "int_ior").data(), int_ior);
			float ext_ior = 1.0f;
			gFromString(get_child_value(local_bsdf, "ext_ior").data(), ext_ior);
			bsdf_instance.mInstanceData.mEta = float3(int_ior / ext_ior);

			load_specular_transmittance();
			load_specular_reflectance();
		}
		else if (local_type == "thindielectric")
		{
			bsdf_instance.mInstanceData.mBSDF = BSDF::ThinDielectric;

			float int_ior = 1.0f;
			gFromString(get_child_value(local_bsdf, "int_ior").data(), int_ior);
			float ext_ior = 1.0f;
			gFromString(get_child_value(local_bsdf, "ext_ior").data(), ext_ior);
			bsdf_instance.mInstanceData.mEta = float3(int_ior / ext_ior);

			load_specular_transmittance();
			load_specular_reflectance();
		}
		else if (local_type == "roughdielectric")
		{
			bsdf_instance.mInstanceData.mBSDF = BSDF::RoughDielectric;

			float int_ior = 1.0f;
			gFromString(get_child_value(local_bsdf, "int_ior").data(), int_ior);
			float ext_ior = 1.0f;
			gFromString(get_child_value(local_bsdf, "ext_ior").data(), ext_ior);
			bsdf_instance.mInstanceData.mEta = float3(int_ior / ext_ior);

			load_specular_transmittance();
			load_specular_reflectance();

			// [TODO] Support beckmann
			// [TODO] Support sample_visible
			gAssert(std::string_view(get_child_value(local_bsdf, "distribution")) == "ggx"); // [TODO] Support beckmann?
			load_alpha();
		}
		else
		{
			bsdf_instance.mInstanceData.mBSDF = BSDF::Unsupported;
			bsdf_instance.mInstanceData.mAlbedo = glm::vec3(0.5f);
		}

		gAssert(id != nullptr);
		bsdf_instance.mInstanceInfo.mMaterialName = id;
		bsdf_instance_by_id[id] = bsdf_instance;

		bsdf = bsdf->NextSiblingElement("bsdf");
	}

	tinyxml2::XMLElement* shape = scene->FirstChildElement("shape");
	while (shape != nullptr)
	{
		std::string_view type = null_to_empty(shape->Attribute("type"));
		std::string_view id = null_to_empty(shape->Attribute("id"));

		SceneContent* primitive = nullptr;
		SceneContent loaded_primitive;
		if (type == "cube")
		{
			primitive = &mPrimitives.mCube;
			id = id.empty() ? "cube" : id;
		}
		else if (type == "rectangle")
		{
			primitive = &mPrimitives.mRectangle;
			id = id.empty() ? "rectangle" : id;
		}
		else if (type == "sphere")
		{
			primitive = &mPrimitives.mSphere;
			id = id.empty() ? "sphere" : id;
		}
		else if (type == "obj")
		{
			std::filesystem::path path = inFilename;
			path.replace_filename(std::filesystem::path(get_child_value(shape, "filename")));
			// Note that mitsuba apply flip_tex_coords on .obj by default
			// See flip_tex_coords https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_shapes.html#wavefront-obj-mesh-loader-obj
			LoadObj(path.string(), glm::mat4x4(1.0f), true, loaded_primitive);
			primitive = &loaded_primitive;
			id = id.empty() ? "obj" : id;
		}

		if (primitive != nullptr)
		{
			uint32_t index_count = static_cast<uint32_t>(primitive->mIndices.size());
			uint32_t vertex_offset = static_cast<uint32_t>(ioSceneContent.mVertices.size());
			uint32_t index_offset = static_cast<uint32_t>(ioSceneContent.mIndices.size());
			for (uint32_t i = 0; i < index_count; i++)
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
					gFromString(matrix_value.data(), matrix);
				}
				else if (tinyxml2::XMLElement* center_element = shape->FirstChildElement("point"))
				{
					tinyxml2::XMLElement* radius_element = shape->FirstChildElement("float");
					gAssert(std::string_view(radius_element->Attribute("name")) == "radius");
					float radius = 0.0f;
					gFromString(radius_element->Attribute("value"), radius);

					gAssert(std::string_view(center_element->Attribute("name")) == "center");
					glm::vec3 center = glm::vec3(0.0f);
					gFromString(center_element->Attribute("x"), center[0]);
					gFromString(center_element->Attribute("y"), center[1]);
					gFromString(center_element->Attribute("z"), center[2]);

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
					auto iter = bsdf_instance_by_id.find(material_id);
					if (iter != bsdf_instance_by_id.end())
					{
						instance_info = iter->second.mInstanceInfo;
						instance_data = iter->second.mInstanceData;
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

					gFromString(get_child_value(emitter, "radiance").data(), instance_data.mEmission);

					instance_data.mBSDF = BSDF::Light;
					instance_data.mTwoSided = false;
					instance_data.mLightIndex = static_cast<uint>(ioSceneContent.mLights.size());

					Light light;
					light.mType = LightType::Count;
					if (primitive == &mPrimitives.mSphere)
						light.mType = LightType::Sphere;
					if (primitive == &mPrimitives.mRectangle)
						light.mType = LightType::Rectangle;

					glm::vec3 translation;
					glm::vec3 skew;
					glm::vec4 perspective;
					glm::quat rotation;
					glm::vec3 scale;
					glm::decompose(matrix, scale, rotation, translation, skew, perspective);

					light.mHalfExtends = glm::vec2(scale.x, scale.y);
					light.mInstanceID = static_cast<uint>(ioSceneContent.mInstanceDatas.size());
					light.mPosition = matrix[3];
					light.mTangent = normalize(matrix[0]);
					light.mBitangent = normalize(matrix[1]);
					light.mNormal = normalize(matrix[2]);
					light.mEmission = instance_data.mEmission;
					ioSceneContent.mLights.push_back(light);
				}
			}

			instance_info.mName = id;
			ioSceneContent.mInstanceInfos.push_back(instance_info);

			instance_data.mTransform = matrix;
			instance_data.mInverseTranspose = glm::transpose(glm::inverse(matrix));
			instance_data.mVertexOffset = 0; // Currently all vertices share same buffer. Only works indices fit in IndexType...
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
		tinyxml2::XMLElement* transform = get_first_child_element_by_name(sensor, "to_world");
		glm::mat4x4 matrix = glm::mat4x4(1.0f);
		gFromString(transform->FirstChildElement("matrix")->Attribute("value"), matrix);
		ioSceneContent.mCameraTransform = matrix;

		float fov = 90.0f;
		gFromString(get_child_value(sensor, "fov").data(), fov);
		ioSceneContent.mFov = fov;
	}

	ioSceneContent.mAtmosphereMode = AtmosphereMode::ConstantColor;

	return true;
}

bool Scene::LoadGLTF(const std::string& inFilename, SceneContent& ioSceneContent)
{
	using namespace tinygltf;
	using namespace glm;

	Model model;
	TinyGLTF loader;
	std::string err;
	std::string warn;
	bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, inFilename);

	if (!warn.empty())
		gTrace(warn.c_str());

	if (!err.empty())
		gTrace(err.c_str());

	if (!ret)
	{
		gTrace("Failed to parse glTF\n");
		return ret;
	}

	auto visit_node = [&](Node& inNode, mat4x4 inParentMatrix)
		{
			mat4x4 matrix = inParentMatrix;

			if (inNode.mesh == -1)
				return matrix;

			mat4x4 T = translate(vec3((float)inNode.translation[0], (float)inNode.translation[1], (float)inNode.translation[2]));
			mat4x4 R = toMat4(quat((float)inNode.rotation[3], (float)inNode.rotation[0], (float)inNode.rotation[1], (float)inNode.rotation[2]));
			mat4x4 S = scale(vec3((float)inNode.scale[0], (float)inNode.scale[1], (float)inNode.scale[2]));
			matrix = T * R * S;

			Mesh mesh = model.meshes[inNode.mesh];

			for (auto&& primitive : mesh.primitives)
			{
				gAssert(primitive.mode == TINYGLTF_MODE_TRIANGLES);

				auto position_attribute = primitive.attributes.find("POSITION");
				gAssert(position_attribute != primitive.attributes.end());
				auto normal_attribute = primitive.attributes.find("NORMAL");
				gAssert(normal_attribute != primitive.attributes.end());
				auto uv_attribute = primitive.attributes.find("TEXCOORD_0");
				gAssert(uv_attribute != primitive.attributes.end());

				Accessor position_accessor = model.accessors[position_attribute->second];
				uint8* position_data = model.buffers[model.bufferViews[position_accessor.bufferView].buffer].data.data() + model.bufferViews[position_accessor.bufferView].byteOffset;
				size_t position_stride = model.bufferViews[position_accessor.bufferView].byteStride;
				position_stride = position_stride == 0 ? sizeof(vec3) : position_stride;
				gAssert(position_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && position_accessor.type == TINYGLTF_TYPE_VEC3);
				gAssert(position_stride * position_accessor.count == model.bufferViews[position_accessor.bufferView].byteLength);

				Accessor normal_accessor = model.accessors[normal_attribute->second];
				uint8* normal_data = model.buffers[model.bufferViews[normal_accessor.bufferView].buffer].data.data() + model.bufferViews[normal_accessor.bufferView].byteOffset;
				size_t normal_stride = model.bufferViews[normal_accessor.bufferView].byteStride;
				normal_stride = normal_stride == 0 ? sizeof(vec3) : normal_stride;
				gAssert(normal_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && normal_accessor.type == TINYGLTF_TYPE_VEC3);
				gAssert(normal_stride * normal_accessor.count == model.bufferViews[normal_accessor.bufferView].byteLength);

				Accessor uv_accessor = model.accessors[uv_attribute->second];
				uint8* uv_data = model.buffers[model.bufferViews[uv_accessor.bufferView].buffer].data.data() + model.bufferViews[uv_accessor.bufferView].byteOffset;
				size_t uv_stride = model.bufferViews[uv_accessor.bufferView].byteStride;
				uv_stride = uv_stride == 0 ? sizeof(vec2) : uv_stride;
				gAssert(uv_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && uv_accessor.type == TINYGLTF_TYPE_VEC2);
				gAssert(uv_stride * uv_accessor.count == model.bufferViews[uv_accessor.bufferView].byteLength);

				gAssert(position_accessor.count == normal_accessor.count);
				gAssert(position_accessor.count == uv_accessor.count);

				uint vertex_offset = (uint)ioSceneContent.mVertices.size();
				uint vertex_count = (uint)position_accessor.count;
				for (uint i = 0; i < vertex_count; i++)
				{
					VertexType vertex;
					memcpy(&vertex, position_data + i * position_stride, sizeof(VertexType));
					ioSceneContent.mVertices.push_back(vertex);

					NormalType normal;
					memcpy(&normal, normal_data + i * normal_stride, sizeof(NormalType));
					ioSceneContent.mNormals.push_back(normal);

					UVType uv;
					memcpy(&uv, uv_data + i * uv_stride, sizeof(UVType));
					ioSceneContent.mUVs.push_back(uv);
				}

				gAssert(primitive.indices != -1);
				Accessor index_accessor = model.accessors[primitive.indices];
				uint8* index_data = model.buffers[model.bufferViews[index_accessor.bufferView].buffer].data.data() + model.bufferViews[index_accessor.bufferView].byteOffset;
				size_t index_stride = model.bufferViews[index_accessor.bufferView].byteStride;
				gAssert(index_accessor.type == TINYGLTF_TYPE_SCALAR && index_stride == 0);

				uint index_offset = (uint)ioSceneContent.mIndices.size();
				uint index_count = (uint)index_accessor.count;
				for (uint i = 0; i < index_count; i++)
				{
					IndexType index;
					if (index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
						index = *reinterpret_cast<uint16*>(index_data + i * sizeof(uint16));
					else if (index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
						index = *reinterpret_cast<uint32*>(index_data + i * sizeof(uint32));
					else
						gAssert(false);
					ioSceneContent.mIndices.push_back(index);
				}

				Material material = model.materials[primitive.material];

				auto get_texture_path = [&](int inIndex)
					{
						if (inIndex == -1)
							return std::filesystem::path();

						std::filesystem::path path = inFilename;
						path.replace_filename(std::filesystem::path(model.images[model.textures[inIndex].source].uri));
						return path;
					};
				InstanceInfo instance_info =
				{
					.mName = std::format("{} - {} - {}", inNode.name, mesh.name, material.name),
					.mMaterialName = material.name,
					.mAlbedoTexture = get_texture_path(material.pbrMetallicRoughness.baseColorTexture.index),
					.mNormalTexture = get_texture_path(material.normalTexture.index),
					.mReflectanceTexture = get_texture_path(material.pbrMetallicRoughness.metallicRoughnessTexture.index),
					.mRoughnessTexture = get_texture_path(material.pbrMetallicRoughness.metallicRoughnessTexture.index),
					.mEmissiveTexture = get_texture_path(material.emissiveTexture.index),
				};
				ioSceneContent.mInstanceInfos.push_back(instance_info);

				InstanceData instance_data =
				{
					.mBSDF = BSDF::glTF,
					.mTwoSided = material.doubleSided,
					.mRoughnessAlpha = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor),
					.mAlbedo = vec3(material.pbrMetallicRoughness.baseColorFactor[0], material.pbrMetallicRoughness.baseColorFactor[1], material.pbrMetallicRoughness.baseColorFactor[2]),
					.mMetallic = static_cast<float>(material.pbrMetallicRoughness.metallicFactor),
					.mEmission = vec3(material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]),
					.mTransform = matrix,
					.mInverseTranspose = transpose(inverse(matrix)),
					.mVertexOffset = vertex_offset,
					.mVertexCount = vertex_count,
					.mIndexOffset = index_offset,
					.mIndexCount = index_count
				};
				ioSceneContent.mInstanceDatas.push_back(instance_data);
			}

			return matrix;
		};

	for (int node_index : model.scenes[model.defaultScene].nodes)
	{
		Node& node = model.nodes[node_index];

		mat4x4 matrix = visit_node(node, mat4x4(1.0f));

		for (int child_node_index : node.children)
			visit_node(model.nodes[child_node_index], matrix);
	}

	return ret;
}

void Scene::FillDummyMaterial(InstanceInfo& ioInstanceInfo, InstanceData& ioInstanceData)
{
	ioInstanceInfo.mMaterialName = "DummyMaterial";
	ioInstanceData.mBSDF = BSDF::Diffuse;
}

void Scene::Load(const std::string_view& inFilePath, const glm::mat4x4& inTransform)
{
	LoadObj("Asset/primitives/cube.obj", glm::mat4x4(1.0f), false, mPrimitives.mCube);
	LoadObj("Asset/primitives/rectangle.obj", glm::mat4x4(1.0f), false, mPrimitives.mRectangle);
	LoadObj("Asset/primitives/sphere.obj", glm::mat4x4(1.0f), false, mPrimitives.mSphere);

	mSceneContent = {}; // Reset

	std::string filename_lower = gToLower(inFilePath);
	if (std::filesystem::exists(filename_lower))
	{
		bool loaded = false;

		if (!loaded && filename_lower.ends_with(".obj"))
			loaded |= LoadObj(filename_lower, inTransform, false, mSceneContent);

		if (!loaded && filename_lower.ends_with(".xml"))
			loaded |= LoadMitsuba(filename_lower, mSceneContent);

		if (!loaded && filename_lower.ends_with(".gltf"))
			loaded |= LoadGLTF(filename_lower, mSceneContent);
	}

	if (mSceneContent.mInstanceDatas.empty())
		LoadDummy(mSceneContent);

	InitializeTextures();
	InitializeBuffers();
	InitializeAccelerationStructures();
	InitializeShaderResourceViews();
}

void Scene::Unload()
{
	mPrimitives = {};
	mSceneContent = {};
	mBlases = {};
	mTLAS = {};
	mBuffers = {};
	mTextures = {};
	mNextSRVIndex = 0;
}

void Scene::Build()
{
	for (auto&& blas : mBlases)
		blas->Build(gCommandList);

	gBarrierUAV(gCommandList, nullptr);

	mTLAS->Build(gCommandList);

	gBarrierUAV(gCommandList, nullptr);
}

void Scene::Render()
{
	for (auto&& texture : mTextures)
		texture.Update();
}

void Scene::InitializeTextures()
{
	std::map<std::string, Texture*> texture_map;
	for (int i = 0; i < mSceneContent.mInstanceDatas.size(); i++)
	{
		InstanceInfo& instance_info = mSceneContent.mInstanceInfos[i];
		InstanceData& instance_data = mSceneContent.mInstanceDatas[i];

		auto get_texture_index = [&](const std::filesystem::path& inPath, bool inSRGB)
			{
				if (inPath.empty())
					return 0u;

				auto iter = texture_map.find(inPath.string());
				if (iter != texture_map.end())
				{
					return (uint)iter->second->mSRVIndex;
				}

				int x = 0, y = 0, n = 0;
				if (stbi_info(inPath.string().c_str(), &x, &y, &n) == 0)
					return 0u;

				mTextures.push_back({});
				Texture& texture = mTextures.back();
				texture.Width(x).Height(y).
					Format(inSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM).
					SRVIndex(ViewDescriptorIndex((uint)ViewDescriptorIndex::SceneAutoSRV + mNextSRVIndex++)).
					Name(inPath.filename().string().c_str()).
					Path(inPath.wstring());
				texture_map[inPath.string()] = &texture;

				return (uint)texture.mSRVIndex;
			};

		instance_data.mAlbedoTextureIndex = get_texture_index(instance_info.mAlbedoTexture, true);
		instance_data.mSpecularReflectanceTextureIndex = get_texture_index(instance_info.mReflectanceTexture, false);
		instance_data.mNormalTextureIndex = get_texture_index(instance_info.mNormalTexture, false);
		instance_data.mEmissionTextureIndex = get_texture_index(instance_info.mEmissiveTexture, true);
	}

	for (auto&& texture : mTextures)
		texture.Initialize();
}

void Scene::InitializeBuffers()
{
	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(0);
	D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

	gAssert(mSceneContent.mIndices.size() <= size_t(1) + std::numeric_limits<IndexType>::max());

	{
		desc.Width = sizeof(IndexType) * mSceneContent.mIndices.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mIndices)));
		gSetName(mBuffers.mIndices, "Scene.", "mBuffers.mIndices", "");

		uint8_t* pData = nullptr;
		mBuffers.mIndices->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mIndices.data(), desc.Width);
		mBuffers.mIndices->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(VertexType) * mSceneContent.mVertices.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mVertices)));
		gSetName(mBuffers.mVertices, "Scene.", "mBuffers.mVertices", "");

		uint8_t* pData = nullptr;
		mBuffers.mVertices->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mVertices.data(), desc.Width);
		mBuffers.mVertices->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(NormalType) * mSceneContent.mNormals.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mNormals)));
		gSetName(mBuffers.mNormals, "Scene.", "mBuffers.mNormals", "");

		uint8_t* pData = nullptr;
		mBuffers.mNormals->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mNormals.data(), desc.Width);
		mBuffers.mNormals->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(UVType) * mSceneContent.mUVs.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mUVs)));
		gSetName(mBuffers.mUVs, "Scene.", "mBuffers.mUVs", "");

		uint8_t* pData = nullptr;
		mBuffers.mUVs->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mUVs.data(), desc.Width);
		mBuffers.mUVs->Unmap(0, nullptr);
	}

	{
		desc.Width = sizeof(InstanceData) * mSceneContent.mInstanceDatas.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mInstanceDatas)));
		gSetName(mBuffers.mInstanceDatas, "Scene.", "mBuffers.mInstanceDatas", "");

		uint8_t* pData = nullptr;
		mBuffers.mInstanceDatas->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mInstanceDatas.data(), desc.Width);
		mBuffers.mInstanceDatas->Unmap(0, nullptr);
	}

	{
		std::vector<Light> dummy; dummy.push_back({}); // Avoid zero byte buffer
		std::vector<Light>& lights = mSceneContent.mLights.empty() ? dummy : mSceneContent.mLights;

		desc.Width = sizeof(Light) * lights.size();
		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mLights)));
		gSetName(mBuffers.mLights, "Scene.", "mBuffers.mLights", "");

		uint8_t* pData = nullptr;
		mBuffers.mLights->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, lights.data(), desc.Width);
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
		instance_descs[instance_index].InstanceContributionToHitGroupIndex = instance_index % 3;
		instance_descs[instance_index].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance_descs[instance_index].AccelerationStructure = blas->GetGPUVirtualAddress();
	}

	mTLAS = std::make_shared<TLAS>();
	mTLAS->Initialize("Default", instance_descs);
}

void Scene::InitializeShaderResourceViews()
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
	create_buffer_SRV(mBuffers.mInstanceDatas.Get(), sizeof(InstanceData), ViewDescriptorIndex::RaytraceInstanceDataSRV);
	create_buffer_SRV(mBuffers.mIndices.Get(), sizeof(IndexType), ViewDescriptorIndex::RaytraceIndicesSRV);
	create_buffer_SRV(mBuffers.mVertices.Get(), sizeof(VertexType), ViewDescriptorIndex::RaytraceVerticesSRV);
	create_buffer_SRV(mBuffers.mNormals.Get(), sizeof(NormalType), ViewDescriptorIndex::RaytraceNormalsSRV);
	create_buffer_SRV(mBuffers.mUVs.Get(), sizeof(UVType), ViewDescriptorIndex::RaytraceUVsSRV);
	create_buffer_SRV(mBuffers.mLights.Get(), sizeof(Light), ViewDescriptorIndex::RaytraceLightsSRV);
}