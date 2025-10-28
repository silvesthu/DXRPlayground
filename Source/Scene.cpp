#include "Scene.h"

#include "Common.h"

#include "Atmosphere.h"
#include "Cloud.h"

#include "Thirdparty/glm/glm/gtx/matrix_decompose.hpp"
#include "Thirdparty/tinyxml2/tinyxml2.h"
#include "Thirdparty/tinygltf/stb_image.h"
#include "Thirdparty/tinyobjloader/tiny_obj_loader.h"
#include "Thirdparty/tiny_gltf.h"
#include "Thirdparty/tinyexr.h"

Scene gScene;

void BLAS::Initialize(const Initializer& inInitializer)
{
	const InstanceInfo& instance_info = inInitializer.mInstanceInfo;
	const InstanceData& instance_data = inInitializer.mInstanceData;

	mDesc = {};
	mDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	mDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
	mDesc.Triangles.VertexBuffer.StartAddress = inInitializer.mVerticesBaseAddress + instance_data.mVertexOffset * sizeof(VertexType);
	mDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(VertexType);
	mDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	mDesc.Triangles.VertexCount = instance_data.mVertexCount;
	if (instance_data.mIndexCount > 0)
	{
		mDesc.Triangles.IndexBuffer = inInitializer.mIndicesBaseAddress + instance_data.mIndexOffset * sizeof(IndexType);
		mDesc.Triangles.IndexCount = instance_data.mIndexCount;
		mDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
	}

	mInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	mInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	mInputs.NumDescs = 1;
	mInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	mInputs.pGeometryDescs = &mDesc;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};

	if (gNVAPI.mLinearSweptSpheresSupported)
	{
		// D3D12_RAYTRACING_GEOMETRY_DESC -> NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX
		if (instance_info.mGeometryType == GeometryType::Triangles)
		{
			mDescEx.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES_EX;
			mDescEx.flags = mDesc.Flags;
			mDescEx.triangles = mDesc.Triangles;
		}
		else if (instance_info.mGeometryType == GeometryType::Sphere)
		{
			mDescEx.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_SPHERES_EX; // see fillD3dSpheresDesc
			mDescEx.flags = mDesc.Flags;

			// [TODO]
		}
		else if (instance_info.mGeometryType == GeometryType::LSS || instance_info.mGeometryType == GeometryType::TriangleAsLSS)
		{
			mDescEx.type = NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_LSS_EX; // see fillD3dLssDesc
			mDescEx.flags = mDesc.Flags;
			mDescEx.lss.vertexCount = instance_data.mVertexCount;
			mDescEx.lss.primitiveCount = instance_data.mLSSIndexCount / 2; // List
			mDescEx.lss.vertexPositionBuffer.StartAddress = inInitializer.mVerticesBaseAddress + instance_data.mVertexOffset * sizeof(VertexType);
			mDescEx.lss.vertexPositionBuffer.StrideInBytes = sizeof(VertexType);
			mDescEx.lss.vertexPositionFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			mDescEx.lss.vertexRadiusBuffer.StartAddress = inInitializer.mLSSRadiiBaseAddress + instance_data.mLSSRadiusOffset * sizeof(RadiusType);
			mDescEx.lss.vertexRadiusBuffer.StrideInBytes = sizeof(RadiusType);
			mDescEx.lss.vertexRadiusFormat = DXGI_FORMAT_R32_FLOAT;

			mDescEx.lss.endcapMode = instance_info.mGeometryType == GeometryType::TriangleAsLSS ? gNVAPI.mWireframeEndcapMode : gNVAPI.mEndcapMode;
			// mDescEx.lss.endcapMode = NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED;
			mDescEx.lss.primitiveFormat = NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_LIST;
			// mDescEx.lss.primitiveFormat = NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT_SUCCESSIVE_IMPLICIT;

			mDescEx.lss.indexBuffer.StartAddress = inInitializer.mLSSIndicesBaseAddress + instance_data.mLSSIndexOffset * sizeof(IndexType);
			mDescEx.lss.indexBuffer.StrideInBytes = sizeof(IndexType);
			mDescEx.lss.indexCount = instance_data.mLSSIndexCount;
			mDescEx.lss.indexFormat = DXGI_FORMAT_R32_UINT;
		}
		
		// D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS -> NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX
		mInputsEx.type = mInputs.Type;
		mInputsEx.flags = (NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS_EX)mInputs.Flags; // EX used by OMM
		mInputsEx.numDescs = mInputs.NumDescs;
		mInputsEx.geometryDescStrideInBytes = sizeof(NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX);
		mInputsEx.descsLayout = mInputs.DescsLayout;
		mInputsEx.pGeometryDescs = &mDescEx;

		NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS params;
		params.version = NVAPI_GET_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO_EX_PARAMS_VER;
		params.pDesc = &mInputsEx;
		params.pInfo = &info;

		gVerify(NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx(gDevice, &params) == NVAPI_OK);
	}
	else
	{
		gDevice->GetRaytracingAccelerationStructurePrebuildInfo(&mInputs, &info);
	}

	D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();
	D3D12_RESOURCE_DESC desc = gGetUAVResourceDesc(info.ScratchDataSizeInBytes);

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mScratch)));
	gSetName(mScratch, "Scene.", instance_info.mName, ".[BLAS].Scratch");

	desc = gGetUAVResourceDesc(info.ResultDataMaxSizeInBytes);
	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&mDest)));
	gSetName(mDest, "Scene.", instance_info.mName, ".[BLAS].Dest");
}

void BLAS::Build(ID3D12GraphicsCommandList4* inCommandList)
{
	if (mBuilt)
		return;

	if (gNVAPI.mLinearSweptSpheresSupported)
	{
		NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX desc = {};
		desc.inputs = mInputsEx;
		desc.destAccelerationStructureData = mDest->GetGPUVirtualAddress();
		desc.sourceAccelerationStructureData = 0;
		desc.scratchAccelerationStructureData = mScratch->GetGPUVirtualAddress();

		NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS params = {};
		params.version = NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS_VER;
		params.pDesc = &desc;
		params.numPostbuildInfoDescs = 0;
		params.pPostbuildInfoDescs = nullptr;
		gVerify(NvAPI_D3D12_BuildRaytracingAccelerationStructureEx(inCommandList, &params) == NVAPI_OK);
	}
	else
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
		desc.Inputs = mInputs;
		desc.DestAccelerationStructureData = mDest->GetGPUVirtualAddress();
		desc.SourceAccelerationStructureData = 0;
		desc.ScratchAccelerationStructureData = mScratch->GetGPUVirtualAddress();
		inCommandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
	}

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
		instance_info.mName = shape.name;
		if (shape.mesh.material_ids.size() == 0 || shape.mesh.material_ids[0] == -1)
		{
			FillDummyMaterial(instance_info, instance_data);
		}
		else
		{
			const tinyobj::material_t& material = reader.GetMaterials()[shape.mesh.material_ids[0]];
			instance_info.mMaterial.mMaterialName = material.name;

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
			instance_data.mReflectance = glm::vec3(material.specular[0], material.specular[1], material.specular[2]);
			instance_data.mSpecularTransmittance = glm::vec3(material.transmittance[0], material.transmittance[1], material.transmittance[2]);
		}

		glm::vec3 translation;
		glm::vec3 skew;
		glm::vec4 perspective;
		glm::quat rotation;
		glm::vec3 scale;
		glm::decompose(inTransform, scale, rotation, translation, skew, perspective);
		instance_info.mDecomposedScale = scale;

		ioSceneContent.mInstanceInfos.push_back(instance_info);

		instance_data.mTransform = inTransform;
		instance_data.mInverseTranspose = glm::transpose(glm::inverse(inTransform)); // [TODO] use adjugate transpose
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

	static auto get_child_type = [](tinyxml2::XMLElement* inElement, const char* inName)
		{
			tinyxml2::XMLElement* child = get_first_child_element_by_name(inElement, inName);
			if (child == nullptr)
				return std::string_view(); // Treat no found the same as attribute with empty string to simplify parsing

			return std::string_view(child->Name());
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

	static auto get_child_sampler = [](tinyxml2::XMLElement* inElement, const char* inName)
		{
			tinyxml2::XMLElement* child = get_first_child_element_by_name(inElement, inName);
			if (child == nullptr)
				return std::string_view();

			return get_child_value(child, "filter_type");
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
		InstanceInfo::Material mMaterial;
		InstanceData mInstanceData;
	};

	std::unordered_map<std::string_view, BSDFInstance> bsdf_instance_by_id;
	tinyxml2::XMLElement* bsdf = scene->FirstChildElement("bsdf");
	while (bsdf != nullptr) // loop to handle nested bsdf
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
				bsdf_instance.mMaterial.mNormalTexture = { .mPath = path };

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
		auto load_specular_reflectance = [&]() { gFromString(get_child_value(local_bsdf, "specular_reflectance").data(), bsdf_instance.mInstanceData.mReflectance); };
		auto load_specular_transmittance = [&]() { gFromString(get_child_value(local_bsdf, "specular_transmittance").data(), bsdf_instance.mInstanceData.mSpecularTransmittance); };
		auto load_eta = [&]() { gFromString(get_child_value(local_bsdf, "eta").data(), bsdf_instance.mInstanceData.mEta); };
		auto load_k = [&]() { gFromString(get_child_value(local_bsdf, "k").data(), bsdf_instance.mInstanceData.mK); };
		// [TODO] Support anisotropy
		auto load_alpha = [&]() { gFromString(get_child_value(local_bsdf, "alpha").data(), bsdf_instance.mInstanceData.mRoughnessAlpha); };

		if (local_type == "diffuse")
		{
			bsdf_instance.mInstanceData.mBSDF = BSDF::Diffuse;

			std::string_view reflectance = get_child_texture(local_bsdf, "reflectance");
			std::string_view sampler = get_child_sampler(local_bsdf, "reflectance");

			std::filesystem::path path = inFilename;
			path.replace_filename(std::filesystem::path(reflectance));

			if (reflectance.empty())
				load_diffuse_reflectance();
			else
			{
				bsdf_instance.mMaterial.mAlbedoTexture = { .mPath = path, .mPointSampler = sampler == "nearest" };
				bsdf_instance.mInstanceData.mAlbedo = glm::vec3(1.0f);
			}
		}
		else if (local_type == "roughconductor")
		{
			bsdf_instance.mInstanceData.mBSDF = BSDF::RoughConductor;

			// [TODO] Support beckmann
			// [TODO] Support sample_visible
			// Treat as ggx for now
			// gAssert(std::string_view(get_child_value(local_bsdf, "distribution")) == "ggx");
			
			load_alpha();

			load_eta();
			load_k();
			
			std::string_view specular_reflectance = get_child_texture(local_bsdf, "specular_reflectance");
			std::string_view sampler = get_child_sampler(local_bsdf, "specular_reflectance");

			std::filesystem::path path = inFilename;
			path.replace_filename(std::filesystem::path(specular_reflectance));

			if (specular_reflectance.empty())
				load_specular_reflectance();
			else
			{
				bsdf_instance.mMaterial.mReflectanceTexture = { .mPath = path, .mPointSampler = sampler == "nearest" };
				bsdf_instance.mInstanceData.mReflectance = glm::vec3(1.0f);
			}
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
		bsdf_instance.mMaterial.mMaterialName = id;
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
				ioSceneContent.mIndices.push_back(primitive->mIndices[i]);

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

			instance_info.mName = id;

			glm::vec3 translation;
			glm::vec3 skew;
			glm::vec4 perspective;
			glm::quat rotation;
			glm::vec3 scale;
			glm::decompose(matrix, scale, rotation, translation, skew, perspective);
			instance_info.mDecomposedScale = scale;

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
						instance_info.mMaterial = iter->second.mMaterial;
						instance_data = iter->second.mInstanceData;
						found_material = true;
					}
				}
				if (!found_material)
					FillDummyMaterial(instance_info, instance_data);
			}

			// Medium
			{
				tinyxml2::XMLElement* medium = shape->FirstChildElement("medium");
				if (medium != nullptr)
				{
					std::string_view medium_type = medium->Attribute("type");
					gAssert(medium_type == "homogeneous"); // heterogeneous not supported

					instance_data.mMedium = 1;

					if (get_child_type(medium, "albedo") == "float")
					{
						float value = 0;
						gFromString(get_child_value(medium, "albedo").data(), value);
						instance_data.mMediumAlbedo = float3(value);
					}
					else
						gFromString(get_child_value(medium, "albedo").data(), instance_data.mMediumAlbedo);
					if (get_child_type(medium, "sigma_t") == "float")
					{
						float value = 0;
						gFromString(get_child_value(medium, "sigma_t").data(), value);
						instance_data.mMediumSigmaT = float3(value);
					}
					else
						gFromString(get_child_value(medium, "sigma_t").data(), instance_data.mMediumSigmaT);

					tinyxml2::XMLElement* phase = medium->FirstChildElement("phase");
					if (phase != nullptr)
					{
						gAssert(false); // Not supported yet

						gFromString(get_child_value(medium, "g").data(), instance_data.mMediumPhase);
					}
				}
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

			ioSceneContent.mInstanceInfos.push_back(instance_info);

			instance_data.mTransform = matrix;
			instance_data.mInverseTranspose = glm::transpose(glm::inverse(matrix));
			instance_data.mVertexOffset = vertex_offset;
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

	auto visit_node = [&](const Node& inNode, std::string& ioName, mat4x4& ioMatrix)
	{
		if (inNode.translation.size() > 0) // not all nodes have transform
		{
			mat4x4 T = translate(vec3((float)inNode.translation[0], (float)inNode.translation[1], (float)inNode.translation[2]));
			mat4x4 R = toMat4(quat((float)inNode.rotation[3], (float)inNode.rotation[0], (float)inNode.rotation[1], (float)inNode.rotation[2]));
			mat4x4 S = scale(vec3((float)inNode.scale[0], (float)inNode.scale[1], (float)inNode.scale[2]));
			ioMatrix = ioMatrix * (T * R * S);	
		}
		ioName = std::format("{}.{}", ioName, inNode.name);

		if (inNode.mesh == -1)
			return;

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
			uint8* position_data = model.buffers[model.bufferViews[position_accessor.bufferView].buffer].data.data()
				+ model.bufferViews[position_accessor.bufferView].byteOffset
				+ position_accessor.byteOffset;
			size_t position_stride = model.bufferViews[position_accessor.bufferView].byteStride;
			position_stride = position_stride == 0 ? sizeof(vec3) : position_stride;
			gAssert(position_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && position_accessor.type == TINYGLTF_TYPE_VEC3);

			Accessor normal_accessor = model.accessors[normal_attribute->second];
			uint8* normal_data = model.buffers[model.bufferViews[normal_accessor.bufferView].buffer].data.data()
				+ model.bufferViews[normal_accessor.bufferView].byteOffset
				+ normal_accessor.byteOffset;
			size_t normal_stride = model.bufferViews[normal_accessor.bufferView].byteStride;
			normal_stride = normal_stride == 0 ? sizeof(vec3) : normal_stride;
			gAssert(normal_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && normal_accessor.type == TINYGLTF_TYPE_VEC3);

			Accessor uv_accessor = model.accessors[uv_attribute->second];
			uint8* uv_data = model.buffers[model.bufferViews[uv_accessor.bufferView].buffer].data.data()
				+ model.bufferViews[uv_accessor.bufferView].byteOffset
				+ uv_accessor.byteOffset;
			size_t uv_stride = model.bufferViews[uv_accessor.bufferView].byteStride;
			uv_stride = uv_stride == 0 ? sizeof(vec2) : uv_stride;
			gAssert(uv_accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && uv_accessor.type == TINYGLTF_TYPE_VEC2);

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
			uint8* index_data = model.buffers[model.bufferViews[index_accessor.bufferView].buffer].data.data()
				+ model.bufferViews[index_accessor.bufferView].byteOffset
				+ index_accessor.byteOffset; 
			size_t index_stride = model.bufferViews[index_accessor.bufferView].byteStride;
			gAssert(index_accessor.type == TINYGLTF_TYPE_SCALAR && index_stride == 0); UNUSED(index_stride);

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
				auto dds = model.textures[inIndex].extensions.find("MSFT_texture_dds");
				if (dds != model.textures[inIndex].extensions.end())
					path.replace_filename(std::filesystem::path(model.images[dds->second.Get("source").GetNumberAsInt()].uri));
				else
					path.replace_filename(std::filesystem::path(model.images[model.textures[inIndex].source].uri));
				return path;
			};

			InstanceInfo instance_info =
			{
				.mName = std::format("{} - {}", ioName, mesh.name),
				.mMaterial = 
				{
					.mMaterialName = material.name,
					.mAlbedoTexture = get_texture_path(material.pbrMetallicRoughness.baseColorTexture.index),
					.mNormalTexture = get_texture_path(material.normalTexture.index),
					.mReflectanceTexture = get_texture_path(material.pbrMetallicRoughness.metallicRoughnessTexture.index),
					.mRoughnessTexture = get_texture_path(material.pbrMetallicRoughness.metallicRoughnessTexture.index),
					.mEmissionTexture = get_texture_path(material.emissiveTexture.index),
				}
			};

			auto pbr_specular_glossiness = material.extensions.find("KHR_materials_pbrSpecularGlossiness");
			if (pbr_specular_glossiness != material.extensions.end())
			{
				auto diffuseTexture = pbr_specular_glossiness->second.Get("diffuseTexture");
				if (diffuseTexture.IsObject())
					instance_info.mMaterial.mAlbedoTexture = { .mPath = get_texture_path(diffuseTexture.Get("index").GetNumberAsInt()) };
				
				auto specularGlossinessTexture = pbr_specular_glossiness->second.Get("specularGlossinessTexture");
				if (specularGlossinessTexture.IsObject())
				{
					instance_info.mMaterial.mReflectanceTexture = { .mPath = get_texture_path(specularGlossinessTexture.Get("index").GetNumberAsInt()) };
					instance_info.mMaterial.mRoughnessTexture = { .mPath = get_texture_path(specularGlossinessTexture.Get("index").GetNumberAsInt()) };
				}
			}

			glm::vec3 translation;
			glm::vec3 skew;
			glm::vec4 perspective;
			glm::quat rotation;
			glm::vec3 scale;
			glm::decompose(ioMatrix, scale, rotation, translation, skew, perspective);
			instance_info.mDecomposedScale = scale;
			
			ioSceneContent.mInstanceInfos.push_back(instance_info);

			InstanceData instance_data =
			{
				.mBSDF = BSDF::pbrMetallicRoughness,
				.mTwoSided = material.doubleSided,
				.mRoughnessAlpha = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor * material.pbrMetallicRoughness.roughnessFactor),
				.mAlbedo = vec3(material.pbrMetallicRoughness.baseColorFactor[0], material.pbrMetallicRoughness.baseColorFactor[1], material.pbrMetallicRoughness.baseColorFactor[2]),
				.mReflectance = vec3(static_cast<float>(material.pbrMetallicRoughness.metallicFactor)),
				.mEmission = vec3(material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]),
				.mTransform = ioMatrix,
				.mInverseTranspose = transpose(inverse(ioMatrix)),
				.mVertexOffset = vertex_offset,
				.mVertexCount = vertex_count,
				.mIndexOffset = index_offset,
				.mIndexCount = index_count
			};

			if (pbr_specular_glossiness != material.extensions.end())
			{
				instance_data.mBSDF = BSDF::pbrSpecularGlossiness;

				auto diffuseFactor = pbr_specular_glossiness->second.Get("diffuseFactor");
				if (diffuseFactor.IsArray())
					instance_data.mAlbedo = vec3(diffuseFactor.Get(0).GetNumberAsDouble(), diffuseFactor.Get(1).GetNumberAsDouble(), diffuseFactor.Get(2).GetNumberAsDouble());

				// [TODO] Need proper conversion
			}
			
			ioSceneContent.mInstanceDatas.push_back(instance_data);

			if (glm::compMax(instance_data.mEmission) > 0.0f)
			{
				ioSceneContent.mEmissiveInstances.push_back({ static_cast<uint>(ioSceneContent.mInstanceDatas.size()) - 1, ioSceneContent.mEmissiveTriangleCount, });
				ioSceneContent.mEmissiveTriangleCount += index_count / kIndexCountPerTriangle;
			}
		}
	};

	auto visit_node_and_children = [&](const auto &self, const Node& inNode, const std::string& inParentName, const mat4x4& inParentMatrix) -> void
	{
		std::string name = inParentName;
		mat4x4 matrix = inParentMatrix;
		visit_node(inNode, name, matrix);
		for (int child_node_index : inNode.children)
			self(self, model.nodes[child_node_index], name, matrix);
	};

	for (int node_index : model.scenes[model.defaultScene].nodes)
		visit_node_and_children(visit_node_and_children, model.nodes[node_index], std::string(), mat4x4(1.0f));

	return ret;
}

void Scene::FillDummyMaterial(InstanceInfo& ioInstanceInfo, InstanceData& ioInstanceData)
{
	ioInstanceInfo.mMaterial.mMaterialName = "DummyMaterial";
	ioInstanceData.mBSDF = BSDF::Diffuse;
}

void Scene::Load(const ScenePreset& inPreset)
{
	LoadObj("Asset/primitives/cube.obj", glm::mat4x4(1.0f), false, mPrimitives.mCube);
	LoadObj("Asset/primitives/rectangle.obj", glm::mat4x4(1.0f), false, mPrimitives.mRectangle);
	LoadObj("Asset/primitives/sphere.obj", glm::mat4x4(1.0f), false, mPrimitives.mSphere);

	mSceneContent = {}; // Reset

	std::string filename_lower = gToLower(inPreset.mPath);
	if (std::filesystem::exists(filename_lower))
	{
		bool loaded = false;

		if (!loaded && filename_lower.ends_with(".obj"))
			loaded |= LoadObj(filename_lower, inPreset.mTransform, false, mSceneContent);

		if (!loaded && filename_lower.ends_with(".xml"))
			loaded |= LoadMitsuba(filename_lower, mSceneContent);

		if (!loaded && filename_lower.ends_with(".gltf"))
			loaded |= LoadGLTF(filename_lower, mSceneContent);
	}

	if (mSceneContent.mInstanceDatas.empty())
		LoadDummy(mSceneContent);

	if (gNVAPI.mLinearSweptSpheresSupported && gNVAPI.mWireframeEnabled && inPreset.mTriangleAsLSSAllowed)
		GenerateLSSFromTriangle();

	InitializeTextures();
	InitializeBuffers();
	InitializeAccelerationStructures();
	InitializeViews();
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
	std::map<std::string, int> texture_map;
	for (int i = 0; i < mSceneContent.mInstanceDatas.size(); i++)
	{
		InstanceInfo& instance_info = mSceneContent.mInstanceInfos[i];
		InstanceData& instance_data = mSceneContent.mInstanceDatas[i];

		auto get_texture_index = [&](InstanceInfo::Material::Texture& inTexture, bool inSRGB) -> TextureInfo
		{
			if (inTexture.empty())
				return {};

			uint sampler_index = inTexture.mPointSampler ? (uint)SamplerDescriptorIndex::PointWrap : (uint)SamplerDescriptorIndex::BilinearWrap;

			auto iter = texture_map.find(inTexture.string());
			if (iter != texture_map.end())
				return { .mTextureIndex = (uint)mTextures[iter->second].mSRVIndex, .mSamplerIndex = sampler_index };

			DXGI_FORMAT format = inSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
			int x = 0, y = 0, n = 0;
			float* exr_data = nullptr;
			if (inTexture.string().ends_with(".exr"))
			{
				const char* err = nullptr;
				if (LoadEXR(&exr_data, &x, &y, inTexture.string().c_str(), &err) != TINYEXR_SUCCESS)
					return {};
				format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			}
			else if (inTexture.string().ends_with(".dds"))
			{
				DirectX::TexMetadata metadata;
				if (FAILED(DirectX::GetMetadataFromDDSFile(inTexture.wstring().c_str(), DirectX::DDS_FLAGS_NONE, metadata)))
					return {};
				x = static_cast<int>(metadata.width);
				y = static_cast<int>(metadata.height);
				format = metadata.format;
			}
			else
			{
				if (stbi_info(inTexture.string().c_str(), &x, &y, &n) == 0)
					return {};
			}

			mTextures.push_back({});
			Texture& texture = mTextures.back();
			texture.Width(x).Height(y).
				Format(format).
				SRVIndex(ViewDescriptorIndex((uint)ViewDescriptorIndex::SceneAutoSRV + mNextSRVIndex++)).
				Name(inTexture.filename().string().c_str()).
				Path(inTexture.wstring());
			texture.mEXRData = exr_data;
			texture_map[inTexture.string()] = static_cast<int>(mTextures.size() - 1);

			return { .mTextureIndex = (uint)texture.mSRVIndex, .mSamplerIndex = sampler_index };
		};

		instance_data.mAlbedoTexture = get_texture_index(instance_info.mMaterial.mAlbedoTexture, true);
		instance_data.mReflectanceTexture = get_texture_index(instance_info.mMaterial.mReflectanceTexture, false);
		instance_data.mNormalTexture = get_texture_index(instance_info.mMaterial.mNormalTexture, false);
		instance_data.mEmissionTexture = get_texture_index(instance_info.mMaterial.mEmissionTexture, true);
	}

	for (auto&& texture : mTextures)
		texture.Initialize();
}

void Scene::InitializeBuffers()
{
	D3D12_RESOURCE_DESC desc_upload = gGetBufferResourceDesc(0);
	D3D12_RESOURCE_DESC desc_uav = gGetUAVResourceDesc(0);
	D3D12_HEAP_PROPERTIES props_upload = gGetUploadHeapProperties();
	D3D12_HEAP_PROPERTIES props_default = gGetDefaultHeapProperties();

	gAssert(mSceneContent.mIndices.size() <= size_t(1) + std::numeric_limits<IndexType>::max());

	{
		desc_upload.Width = sizeof(IndexType) * mSceneContent.mIndices.size();
		gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mIndices)));
		gSetName(mBuffers.mIndices, "Scene.", "mBuffers.mIndices", "");

		uint8_t* pData = nullptr;
		mBuffers.mIndices->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mIndices.data(), desc_upload.Width);
		mBuffers.mIndices->Unmap(0, nullptr);
	}

	{
		desc_upload.Width = sizeof(VertexType) * mSceneContent.mVertices.size();
		gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mVertices)));
		gSetName(mBuffers.mVertices, "Scene.", "mBuffers.mVertices", "");

		uint8_t* pData = nullptr;
		mBuffers.mVertices->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mVertices.data(), desc_upload.Width);
		mBuffers.mVertices->Unmap(0, nullptr);
	}

	{
		desc_upload.Width = sizeof(NormalType) * mSceneContent.mNormals.size();
		gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mNormals)));
		gSetName(mBuffers.mNormals, "Scene.", "mBuffers.mNormals", "");

		uint8_t* pData = nullptr;
		mBuffers.mNormals->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mNormals.data(), desc_upload.Width);
		mBuffers.mNormals->Unmap(0, nullptr);
	}

	{
		desc_upload.Width = sizeof(UVType) * mSceneContent.mUVs.size();
		gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mUVs)));
		gSetName(mBuffers.mUVs, "Scene.", "mBuffers.mUVs", "");

		uint8_t* pData = nullptr;
		mBuffers.mUVs->Map(0, nullptr, reinterpret_cast<void**>(&pData));
		memcpy(pData, mSceneContent.mUVs.data(), desc_upload.Width);
		mBuffers.mUVs->Unmap(0, nullptr);
	}

	{
		desc_upload.Width = sizeof(InstanceData)  * gMax(1ull, mSceneContent.mInstanceDatas.size());
		gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mInstanceDatas)));
		gSetName(mBuffers.mInstanceDatas, "Scene.", "mBuffers.mInstanceDatas", "");

		if (!mSceneContent.mInstanceDatas.empty())
		{
			uint8_t* pData = nullptr;
			mBuffers.mInstanceDatas->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			memcpy(pData, mSceneContent.mInstanceDatas.data(), desc_upload.Width);
			mBuffers.mInstanceDatas->Unmap(0, nullptr);
		}
	}

	{
		desc_upload.Width = sizeof(Light) * gMax(1ull, mSceneContent.mLights.size());
		gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mLights)));
		gSetName(mBuffers.mLights, "Scene.", "mBuffers.mLights", "");

		if (!mSceneContent.mLights.empty())
		{
			uint8_t* pData = nullptr;
			mBuffers.mLights->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			memcpy(pData, mSceneContent.mLights.data(), desc_upload.Width);
			mBuffers.mLights->Unmap(0, nullptr);
		}
	}

	// RTXDI
	{
		{
			desc_upload.Width = sizeof(PrepareLightsTask) * gMax(1ull, mSceneContent.mEmissiveInstances.size());
			gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mTaskBuffer)));
			gSetName(mBuffers.mTaskBuffer, "Scene.", "mBuffers.mTaskBuffer", "");

			if (!mSceneContent.mEmissiveInstances.empty())
			{
				// See PrepareLightsPass::Process
				// Handle here for now since Scene changes after load is not supported yet
				uint light_buffer_offset = 0;
				for (auto&& emissive_instance : mSceneContent.mEmissiveInstances)
				{
					auto& instance_data = mSceneContent.mInstanceDatas[emissive_instance.mInstanceIndex];

					PrepareLightsTask task;
					task.mInstanceIndex = emissive_instance.mInstanceIndex;
					task.mGeometryIndex = emissive_instance.mInstanceIndex; // Currently only 1 geometry per instance is supported
					task.mTriangleCount = instance_data.mIndexCount / kVertexCountPerTriangle;
					task.mLightBufferOffset = light_buffer_offset;

					mBuffers.mTaskBufferCPU.push_back(task);

					light_buffer_offset += task.mTriangleCount;
				}

				uint8_t* pData = nullptr;
				mBuffers.mTaskBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pData));
				memcpy(pData, mBuffers.mTaskBufferCPU.data(), desc_upload.Width);
				mBuffers.mTaskBuffer->Unmap(0, nullptr);
			}
		}

		{
			desc_uav.Width = sizeof(RAB_LightInfo) * gMax(1u, mSceneContent.mEmissiveTriangleCount);
			gValidate(gDevice->CreateCommittedResource(&props_default, D3D12_HEAP_FLAG_NONE, &desc_uav, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mBuffers.mLightDataBuffer)));
			gSetName(mBuffers.mLightDataBuffer, "Scene.", "mBuffers.mLightDataBuffer", "");
		}
	}
}

void Scene::GenerateLSSFromTriangle()
{
	D3D12_RESOURCE_DESC desc_upload = gGetBufferResourceDesc(0);
	D3D12_HEAP_PROPERTIES props_upload = gGetUploadHeapProperties();

	constexpr uint indices_per_triangle = 3;									// 1 triangles = 3 indices
	constexpr uint lss_indices_per_edge = 2;									// List
	constexpr uint lss_indices_per_lss_triangle = 3 * lss_indices_per_edge;		// 1 triangles -> 3 LSS = 6 indices
	gAssert(mSceneContent.mIndices.size() % indices_per_triangle == 0);

	size_t instance_count = mSceneContent.mInstanceDatas.size();
	gAssert(instance_count == mSceneContent.mInstanceInfos.size());
	for (uint instance_index = 0; instance_index < instance_count; instance_index++)
	{
		InstanceData& instance_data = mSceneContent.mInstanceDatas[instance_index];
		if (gMaxComponent(instance_data.mEmission) > 0.0f)
			continue; // Leave light source as triangles

		instance_data.mLSSIndexCount = instance_data.mIndexCount * 2;
		instance_data.mLSSIndexOffset = instance_data.mIndexOffset * 2;

		instance_data.mLSSRadiusCount = instance_data.mVertexCount;
		instance_data.mLSSRadiusOffset = instance_data.mVertexOffset;

		InstanceInfo& instance_info = mSceneContent.mInstanceInfos[instance_index];

		instance_info.mGeometryType = GeometryType::TriangleAsLSS;
	}

	{
		{
			size_t triangle_count = mSceneContent.mIndices.size() / indices_per_triangle;

			std::vector<IndexType> LSSIndices;
			LSSIndices.resize(triangle_count * lss_indices_per_lss_triangle);
			for (uint triangle_index = 0; triangle_index < triangle_count; triangle_index++)
			{
				uint triangle_index_offset = triangle_index * indices_per_triangle;
				IndexType v0 = mSceneContent.mIndices[triangle_index_offset + 0];
				IndexType v1 = mSceneContent.mIndices[triangle_index_offset + 1];
				IndexType v2 = mSceneContent.mIndices[triangle_index_offset + 2];

				uint lss_triangle_index_offset = triangle_index * lss_indices_per_lss_triangle;
				LSSIndices[lss_triangle_index_offset + 0] = v0;
				LSSIndices[lss_triangle_index_offset + 1] = v1;
				LSSIndices[lss_triangle_index_offset + 2] = v1;
				LSSIndices[lss_triangle_index_offset + 3] = v2;
				LSSIndices[lss_triangle_index_offset + 4] = v2;
				LSSIndices[lss_triangle_index_offset + 5] = v0;
			}

			desc_upload.Width = sizeof(IndexType) * LSSIndices.size();
			gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mLSSIndices)));
			gSetName(mBuffers.mLSSIndices, "Scene.", "mBuffers.mLSSIndices", "");

			uint8_t* pData = nullptr;
			mBuffers.mLSSIndices->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			memcpy(pData, LSSIndices.data(), desc_upload.Width);
			mBuffers.mLSSIndices->Unmap(0, nullptr);
		}

		{
			// [NOTE] Radius can also be uniform by use stride = 0, see NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC
			std::vector<float> LSSRadii;
			LSSRadii.resize(mSceneContent.mVertices.size());
			// std::fill(LSSRadii.begin(), LSSRadii.end(), gNVAPI.mWireframeRadius);
			for (uint instance_index = 0; instance_index < instance_count; instance_index++)
			{
				// Assume vertex is not shared between instances
				float wireframe_radius = gNVAPI.mWireframeRadius * 1.0f / gMinComponent(mSceneContent.mInstanceInfos[instance_index].mDecomposedScale);
				for (uint vertex_index = 0; vertex_index < mSceneContent.mInstanceDatas[instance_index].mVertexCount; vertex_index++)
					LSSRadii[mSceneContent.mInstanceDatas[instance_index].mVertexOffset + vertex_index] = wireframe_radius;
			}

			desc_upload.Width = sizeof(RadiusType) * LSSRadii.size();
			gValidate(gDevice->CreateCommittedResource(&props_upload, D3D12_HEAP_FLAG_NONE, &desc_upload, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mBuffers.mLSSRadii)));
			gSetName(mBuffers.mLSSRadii, "Scene.", "mBuffers.mLSSRadii", "");

			uint8_t* pData = nullptr;
			mBuffers.mLSSRadii->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			memcpy(pData, LSSRadii.data(), desc_upload.Width);
			mBuffers.mLSSRadii->Unmap(0, nullptr);
		}
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
		blas->Initialize(
		{
			.mInstanceInfo = instance_info,
			.mInstanceData = instance_data,
			.mVerticesBaseAddress = mBuffers.mVertices->GetGPUVirtualAddress(),
			.mIndicesBaseAddress = mBuffers.mIndices->GetGPUVirtualAddress(),
			.mLSSIndicesBaseAddress = mBuffers.mLSSIndices != nullptr ? mBuffers.mLSSIndices->GetGPUVirtualAddress() : 0,
			.mLSSRadiiBaseAddress = mBuffers.mLSSRadii != nullptr ? mBuffers.mLSSRadii->GetGPUVirtualAddress() : 0,
		});
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

void Scene::InitializeViews()
{
	auto create_acceleration_structure_SRV = [](ID3D12Resource* inResource, ViewDescriptorIndex inViewDescriptorIndex)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
		desc.RaytracingAccelerationStructure.Location = inResource->GetGPUVirtualAddress();

		for (int i = 0; i < kFrameInFlightCount; i++)
			gDevice->CreateShaderResourceView(nullptr, &desc, gFrameContexts[i].mViewDescriptorHeap.GetCPUHandle(inViewDescriptorIndex));
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

		for (int i = 0; i < kFrameInFlightCount; i++)
			gDevice->CreateShaderResourceView(inResource, &desc, gFrameContexts[i].mViewDescriptorHeap.GetCPUHandle(inViewDescriptorIndex));
	};

	auto create_buffer_UAV = [](ID3D12Resource* inResource, int inStride, ViewDescriptorIndex inViewDescriptorIndex)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
		D3D12_RESOURCE_DESC resource_desc = inResource->GetDesc();
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		desc.Buffer.NumElements = static_cast<UINT>(resource_desc.Width / inStride);
		desc.Buffer.StructureByteStride = inStride;
		desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

		for (int i = 0; i < kFrameInFlightCount; i++)
			gDevice->CreateUnorderedAccessView(inResource, nullptr, &desc, gFrameContexts[i].mViewDescriptorHeap.GetCPUHandle(inViewDescriptorIndex));
	};
	
	create_buffer_SRV(mBuffers.mInstanceDatas.Get(), sizeof(InstanceData), ViewDescriptorIndex::RaytraceInstanceDataSRV);
	create_buffer_SRV(mBuffers.mIndices.Get(), sizeof(IndexType), ViewDescriptorIndex::RaytraceIndicesSRV);
	create_buffer_SRV(mBuffers.mVertices.Get(), sizeof(VertexType), ViewDescriptorIndex::RaytraceVerticesSRV);
	create_buffer_SRV(mBuffers.mNormals.Get(), sizeof(NormalType), ViewDescriptorIndex::RaytraceNormalsSRV);
	create_buffer_SRV(mBuffers.mUVs.Get(), sizeof(UVType), ViewDescriptorIndex::RaytraceUVsSRV);
	create_buffer_SRV(mBuffers.mLights.Get(), sizeof(Light), ViewDescriptorIndex::RaytraceLightsSRV);

	// RTXDI - minimal-sample
	create_buffer_SRV(mBuffers.mTaskBuffer.Get(), sizeof(PrepareLightsTask), ViewDescriptorIndex::TaskBufferSRV);
	create_buffer_SRV(mBuffers.mLightDataBuffer.Get(), sizeof(RAB_LightInfo), ViewDescriptorIndex::LightDataBufferSRV);
	create_buffer_UAV(mBuffers.mLightDataBuffer.Get(), sizeof(RAB_LightInfo), ViewDescriptorIndex::LightDataBufferUAV);
}