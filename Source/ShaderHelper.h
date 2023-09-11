#pragma once
#include "Common.h"

#include <fstream>
#include <sstream>

ComPtr<ID3D12RootSignature> gCreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc)
{
	ComPtr<ID3DBlob> signature_blob;
	ComPtr<ID3DBlob> error_blob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
	if (FAILED(hr))
	{
		std::string str((char*)error_blob->GetBufferPointer(), error_blob->GetBufferSize());
		gTrace(str.c_str());
		assert(false);
		return nullptr;
	}
	ComPtr<ID3D12RootSignature> root_signature;
	if (FAILED(gDevice->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature))))
		assert(false);
	return root_signature;
}

// Hold data for D3D12_STATE_SUBOBJECT
template <typename DescType, D3D12_STATE_SUBOBJECT_TYPE SubObjectType>
struct StateSubobjectHolder
{
	StateSubobjectHolder()
	{
		mStateSubobject.Type = SubObjectType;
		mStateSubobject.pDesc = &mDesc;
	}

	StateSubobjectHolder(const StateSubobjectHolder& inOther)
	{
		*this = inOther;
	}

	StateSubobjectHolder& operator=(const StateSubobjectHolder& inOther)
	{
		gAssert(mStateSubobject.Type == inOther.mStateSubobject.Type);
		mDesc = inOther.mDesc;

		// mStateSubobject.pDesc is referencing this instance, make sure it is never copied.

		return *this;
	}

	D3D12_STATE_SUBOBJECT mStateSubobject = {};

protected:
	DescType mDesc = {};
};

// Shader binary
struct DXILLibrary : public StateSubobjectHolder<D3D12_DXIL_LIBRARY_DESC, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY>
{
	DXILLibrary(IDxcBlob* inShaderBlob, const wchar_t* inEntryPoint[], glm::uint32 inEntryPointCount) : mShaderBlob(inShaderBlob)
	{
		mExportDescs.resize(inEntryPointCount);
		mExportNames.resize(inEntryPointCount);

		if (inShaderBlob)
		{
			mDesc.DXILLibrary.pShaderBytecode = inShaderBlob->GetBufferPointer();
			mDesc.DXILLibrary.BytecodeLength = inShaderBlob->GetBufferSize();
			mDesc.NumExports = inEntryPointCount;
			mDesc.pExports = mExportDescs.data();

			for (glm::uint32 i = 0; i < inEntryPointCount; i++)
			{
				mExportNames[i] = inEntryPoint[i];
				mExportDescs[i].Name = mExportNames[i].c_str();
				mExportDescs[i].Flags = D3D12_EXPORT_FLAG_NONE;
				mExportDescs[i].ExportToRename = nullptr;
			}
		}
	}

private:
	ComPtr<IDxcBlob> mShaderBlob;
	std::vector<D3D12_EXPORT_DESC> mExportDescs;
	std::vector<std::wstring> mExportNames;
};

// Ray tracing shader structure
struct HitGroup : public StateSubobjectHolder<D3D12_HIT_GROUP_DESC, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP>
{
	HitGroup() {}
	HitGroup(const wchar_t* inAnyHitShaderImport, const wchar_t* inClosestHitShaderImport, const wchar_t* inHitGroupExport)
	{
		mDesc.AnyHitShaderImport = inAnyHitShaderImport;
		mDesc.ClosestHitShaderImport = inClosestHitShaderImport;
		mDesc.HitGroupExport = inHitGroupExport;
		mDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
		mDesc.IntersectionShaderImport = nullptr; // not used
	}
};

// Local root signature - Shader input
struct LocalRootSignature : public StateSubobjectHolder<ID3D12RootSignature*, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE>
{
	LocalRootSignature() {}
	LocalRootSignature(const D3D12_ROOT_SIGNATURE_DESC& inDesc)
	{
		mRootSignature = gCreateRootSignature(inDesc);
		mRootSignature->SetName(L"LocalRootSignature");
		mDesc = mRootSignature.Get();
	}
private:
	ComPtr<ID3D12RootSignature> mRootSignature; // Necessary?
};

// Associate subobject to exports - Shader entry point -> Shader input
struct SubobjectToExportsAssociation : public StateSubobjectHolder<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION>
{
	SubobjectToExportsAssociation() {}
	SubobjectToExportsAssociation(const wchar_t* inExportNames[], glm::uint32 inExportCount, const D3D12_STATE_SUBOBJECT* inSubobjectToAssociate)
	{
		mDesc.NumExports = inExportCount;
		mDesc.pExports = inExportNames;
		mDesc.pSubobjectToAssociate = inSubobjectToAssociate;
	}
};

// String literals
static const wchar_t* kGenerationShaderName		= L"RayGeneration";
static const wchar_t* kMissShaderName			= L"Miss";
static const wchar_t* kHitShaderName			= L"Hit";
static const wchar_t* kBaseShaderNames[]		= { kGenerationShaderName, kMissShaderName, kHitShaderName };
static const wchar_t* kBaseHitShaderNames[]		= { kHitShaderName };
static const wchar_t* kHitShaderNames[]			= { L"Hit100", L"Hit010", L"Hit001" };

std::wstring gGetHitGroupName(const wchar_t* inHitShaderName)
{
	std::wstring name = inHitShaderName;
	name += L"Group";
	return name;
}

ComPtr<ID3D12StateObject> gCreateLibStateObject(ShaderLibType inShaderLibType, IDxcBlob* inBlob, ID3D12RootSignature* inGlobalRootSignature)
{
	// See D3D12_STATE_SUBOBJECT_TYPE
	// https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#d3d12_state_subobject_type
	// Note that all pointers should be valid until CreateStateObject

	std::array<D3D12_STATE_SUBOBJECT, 64> subobjects;
	glm::uint32 index = 0;

	std::vector<const wchar_t*> entry_points;
	if (inShaderLibType == ShaderLibType::Base)
	{
		entry_points.insert(entry_points.end(), std::begin(kBaseShaderNames), std::end(kBaseShaderNames));
	}
	else
	{
		entry_points.insert(entry_points.end(), std::begin(kHitShaderNames), std::end(kHitShaderNames));
	}
	DXILLibrary dxilLibrary(inBlob, entry_points.data(), static_cast<glm::uint32>(entry_points.size()));
	subobjects[index++] = dxilLibrary.mStateSubobject;

	// Hit group, allocate objects upfront to ensure pointers are valid by the end of this function
	constexpr int kHitShaderReserveCount = static_cast<int>(gMax(std::size(kBaseHitShaderNames), std::size(kHitShaderNames)));
	std::array<std::wstring, kHitShaderReserveCount> hit_group_names;
	std::array<HitGroup, kHitShaderReserveCount> hit_groups;
	std::array<LocalRootSignature, kHitShaderReserveCount> local_root_signatures;
	std::array<SubobjectToExportsAssociation, kHitShaderReserveCount> export_associations;

	int hit_shader_count = static_cast<int>(inShaderLibType == ShaderLibType::Base ? std::size(kBaseHitShaderNames) : std::size(kHitShaderNames));
	const wchar_t** hit_shader_names = inShaderLibType == ShaderLibType::Base ? kBaseHitShaderNames : kHitShaderNames;
	for (int hit_shader_index = 0; hit_shader_index < hit_shader_count; hit_shader_index++)
	{
		hit_group_names[hit_shader_index] = gGetHitGroupName(hit_shader_names[hit_shader_index]);

		hit_groups[hit_shader_index] = HitGroup(nullptr, hit_shader_names[hit_shader_index], hit_group_names[hit_shader_index].c_str());
		subobjects[index++] = hit_groups[hit_shader_index].mStateSubobject;

		// Local signature desc, not used and can be omitted.
		D3D12_ROOT_SIGNATURE_DESC local_signature_desc = {};
		local_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		local_root_signatures[hit_shader_index] = LocalRootSignature(local_signature_desc);
		subobjects[index++] = local_root_signatures[hit_shader_index].mStateSubobject;
		export_associations[hit_shader_index] = SubobjectToExportsAssociation(&hit_shader_names[hit_shader_index], 1, &(subobjects[index - 1]));
		subobjects[index++] = export_associations[hit_shader_index].mStateSubobject;
	}

	// Shader config
	D3D12_RAYTRACING_SHADER_CONFIG shader_config = { .MaxPayloadSizeInBytes = (glm::uint32)sizeof(RayPayload), .MaxAttributeSizeInBytes = sizeof(float) * 2 /* sizeof(BuiltInTriangleIntersectionAttributes) */};
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config };

	// Pipeline config, [0, 31] https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_raytracing_pipeline_config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = { .MaxTraceRecursionDepth = 1 }; // 1 = TraceRay only from raygeneration
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config };

	// Global root signature
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &inGlobalRootSignature };

	// State object config
	D3D12_STATE_OBJECT_CONFIG state_object_config = { .Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &state_object_config };

	// Create the state object
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();
	desc.Type = inShaderLibType == ShaderLibType::Base ? D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE : D3D12_STATE_OBJECT_TYPE_COLLECTION;

	ComPtr<ID3D12StateObject> state_object;
	if (FAILED(gDevice->CreateStateObject(&desc, IID_PPV_ARGS(&state_object))))
		return nullptr;

	return state_object;
}

ComPtr<ID3D12StateObject> gCombineLibStateObject(ID3D12StateObject* inBaseStateObject, ID3D12StateObject* inCollections)
{
	std::array<D3D12_STATE_SUBOBJECT, 64> subobjects;
	glm::uint32 index = 0;

	D3D12_STATE_OBJECT_CONFIG state_object_config = { .Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &state_object_config };

	D3D12_EXISTING_COLLECTION_DESC collection_desc = { .pExistingCollection = inCollections };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION, &collection_desc };

	D3D12_STATE_OBJECT_DESC desc = {};
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();

	ComPtr<ID3D12StateObject> state_object;
	gValidate(gDevice->AddToStateObject(
		&desc,
		inBaseStateObject,
		IID_PPV_ARGS(state_object.GetAddressOf())));
	return state_object;
}

struct ShaderIdentifier
{
	uint8_t identifier[32];
};

struct ShaderTableEntry
{
	ShaderIdentifier mShaderIdentifier;

	union RootArgument
	{
		D3D12_GPU_VIRTUAL_ADDRESS mAddress;
		uint64_t mHandle;
	};

	RootArgument mRootArgument = {};
	uint8_t mPadding[24] = {};
};
static_assert(sizeof(ShaderTableEntry::mShaderIdentifier) == D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, "D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES check failed");
static_assert(sizeof(ShaderTableEntry) == D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, "D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT check failed");

ShaderTable gCreateShaderTable(const Shader& inShader)
{
	ShaderTable shader_table;
	if (inShader.mData.mStateObject == nullptr)
		return shader_table;

	// Construct the table
	ShaderTableEntry shader_table_entries[32];
	glm::uint32 shader_table_entry_index = 0;
	{
		// Shader table between all ray tracing shader types, by adding offset when reference the table
		// So it is necessary to align up to table size rather than record size (entry size)

		// Local root argument may be embedded along with shader identifier
		// https://github.com/NVIDIAGameWorks/DxrTutorials/blob/dcb8810086f80e77157a6a3b7deff2f24e0986d7/Tutorials/06-Raytrace/06-Raytrace.cpp#L734
		// https://github.com/NVIDIAGameWorks/Falcor/blob/236927c2bca252f9ea1e3bacb982f8fcba817a67/Framework/Source/Experimental/Raytracing/RtProgramVars.cpp#L116
		// p20 http://intro-to-dxr.cwyman.org/presentations/IntroDXR_RaytracingAPI.pdf
		// http://intro-to-dxr.cwyman.org/spec/DXR_FunctionalSpec_v0.09.docx

		// Signature -> DescriptorHeap
		// * Global - SetDescriptorHeaps() - D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE
		// * Per Shader - SetDescriptorHeaps() - D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE
		// * Per Shader Instance - Shader Table (DescriptorHeap or raw address) - D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE

		ComPtr<ID3D12StateObjectProperties> state_object_properties;
		inShader.mData.mStateObject->QueryInterface(IID_PPV_ARGS(&state_object_properties));

		// RayGen shaders
		{
			shader_table.mRayGenOffset = shader_table_entry_index;

			memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(kGenerationShaderName), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entry_index++;

			shader_table.mRayGenCount = shader_table_entry_index - shader_table.mRayGenOffset;
		}

		// Miss shaders
		{
			shader_table.mMissOffset = shader_table_entry_index;

			memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(kMissShaderName), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entry_index++;

			shader_table.mMissCount = shader_table_entry_index - shader_table.mMissOffset;
		}

		// HitGroup shaders
		{
			shader_table.mHitGroupOffset = shader_table_entry_index;

			for (int i = 0; i < static_cast<int>(std::size(kBaseHitShaderNames)); i++)
			{
				std::wstring shader_group_name = gGetHitGroupName(kBaseHitShaderNames[i]);
				memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(shader_group_name.c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
				shader_table_entry_index++;
			}

			if (gRenderer.mUseLibHitShader)
			{
				for (int i = 0; i < static_cast<int>(std::size(kHitShaderNames)); i++)
				{
					std::wstring shader_group_name = gGetHitGroupName(kHitShaderNames[i]);
					memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(shader_group_name.c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
					shader_table_entry_index++;
				}
			}

			shader_table.mHitGroupCount = shader_table_entry_index - shader_table.mHitGroupOffset;
		}

		shader_table.mEntrySize = sizeof(ShaderTableEntry);
	}

	// Create the table
	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(shader_table.mEntrySize * shader_table_entry_index);

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&shader_table.mResource)));
		shader_table.mResource->SetName(L"ShaderTable");
	}

	// Copy the table
	{
		// Map
		uint8_t* data_pointer;
		gValidate(shader_table.mResource->Map(0, nullptr, (void**)&data_pointer));

		// Copy
		memcpy(data_pointer, shader_table_entries, shader_table.mEntrySize * shader_table_entry_index);

		// Unmap
		shader_table.mResource->Unmap(0, nullptr);
	}

	return shader_table;
}


IDxcBlob* gCompileShader(const char* inFilename, const char* inEntryPoint, const char* inProfile)
{
	// Load shader
	std::ifstream shader_file(inFilename);
	std::stringstream shader_stream;
	shader_stream << shader_file.rdbuf();
	std::string shader_string = shader_stream.str();

	// LoadLibraryW + GetProcAddress to eliminate dependency on .lib. Make updating .dll easier.
	HMODULE dll = LoadLibraryW(L"dxcompiler.dll");
	if (dll == NULL)
		return nullptr;
	DxcCreateInstanceProc DxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dll, "DxcCreateInstance"));

	// See https://simoncoenen.com/blog/programming/graphics/DxcRevised.html
	ComPtr<IDxcUtils> utils;
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils.GetAddressOf()));

	IDxcBlobEncoding* blob_encoding;
	gValidate(utils->CreateBlobFromPinned(shader_string.c_str(), static_cast<glm::uint32>(shader_string.length()), CP_UTF8, &blob_encoding));

	ComPtr<IDxcCompiler> compiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.GetAddressOf()));

	ComPtr<IDxcIncludeHandler> include_handler;
	utils->CreateDefaultIncludeHandler(include_handler.GetAddressOf());

	std::string filename(inFilename);
	std::wstring wfilename(filename.begin(), filename.end());

	std::vector<DxcDefine> defines;
	std::wstring profile = gToWString(inProfile);
	DxcDefine dxc_define_profile{};
	dxc_define_profile.Name = L"SHADER_PROFILE_UNKNOWN";
	dxc_define_profile.Value = L"1";
	if (profile._Starts_with(L"lib"))
		dxc_define_profile.Name = L"SHADER_PROFILE_LIB";
	if (profile._Starts_with(L"cs"))
		dxc_define_profile.Name = L"SHADER_PROFILE_CS";
	if (profile._Starts_with(L"ps"))
		dxc_define_profile.Name = L"SHADER_PROFILE_PS";
	if (profile._Starts_with(L"vs"))
		dxc_define_profile.Name = L"SHADER_PROFILE_VS";
	defines.push_back(dxc_define_profile);

	std::wstring entry_point = gToWString(inEntryPoint);
	std::wstring entry_point_macro = L"ENTRY_POINT_";
	entry_point_macro += entry_point;
	DxcDefine dxc_define_entry_point{};
	dxc_define_entry_point.Name = entry_point_macro.c_str();
	defines.push_back(dxc_define_entry_point);

	std::vector<LPCWSTR> arguments;
	arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);				// -WX
	arguments.push_back(DXC_ARG_ALL_RESOURCES_BOUND);				// -all_resources_bound
	arguments.push_back(DXC_ARG_DEBUG);								// -Zi
	arguments.push_back(L"-Qembed_debug");							// -Qembed_debug
	arguments.push_back(L"-HV 2021");								// -HV 2021

	IDxcOperationResult* operation_result;
	if (FAILED(compiler->Compile(
		blob_encoding,												// program text
		wfilename.c_str(),											// file name, mostly for error messages
		entry_point.c_str(),										// entry point function
		profile.c_str(),											// target profile
		arguments.data(), static_cast<UINT32>(arguments.size()),	// compilation arguments and their count
		defines.data(), static_cast<UINT32>(defines.size()),		// name/value defines and their count
		include_handler.Get(),										// handler for #include directives
		&operation_result)))
		assert(false);

	HRESULT compile_result;
	gValidate(operation_result->GetStatus(&compile_result));

	if (FAILED(compile_result))
	{
		IDxcBlobEncoding* blob = nullptr;
		IDxcBlobUtf8* blob_8 = nullptr;
		gValidate(operation_result->GetErrorBuffer(&blob));
		// We can use the library to get our preferred encoding.
		gValidate(utils->GetBlobAsUtf8(blob, &blob_8));
		std::string str((char*)blob_8->GetBufferPointer(), blob_8->GetBufferSize() - 1);
		gTrace(str.c_str());
		blob->Release();
		blob_8->Release();
		return nullptr;
	}

	IDxcBlob* blob = nullptr;
	gValidate(operation_result->GetResult(&blob));

	if (gRenderer.mDumpDisassemblyRayQuery && std::string_view("RayQueryCS") == inEntryPoint)
	{
		IDxcBlob* blob_to_dissemble = blob;
		IDxcBlobEncoding* disassemble = nullptr;
		IDxcBlobUtf8* blob_8 = nullptr;
		compiler->Disassemble(blob_to_dissemble, &disassemble);
		gValidate(utils->GetBlobAsUtf8(disassemble, &blob_8));
		std::string str((char*)blob_8->GetBufferPointer(), blob_8->GetBufferSize() - 1);

		static int counter = 0;
		std::filesystem::path path = gCreateDumpFolder();
		path += "RayQueryCS_";
		path += std::to_string(counter++);
		path += ".txt";
		std::ofstream stream(path);
		stream << str;
		stream.close();

		gRenderer.mDumpDisassemblyRayQuery = false;
	}

	return blob;
}

bool gCreateVSPSPipelineState(const char* inShaderFileName, const char* inVSName, const char* inPSName, Shader& ioSystemShader)
{
	IDxcBlob* vs_blob = gCompileShader(inShaderFileName, inVSName, "vs_6_6");
	IDxcBlob* ps_blob = gCompileShader(inShaderFileName, inPSName, "ps_6_6");
	if (vs_blob == nullptr || ps_blob == nullptr)
		return false;

	if (FAILED(gDevice->CreateRootSignature(0, ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), IID_PPV_ARGS(&ioSystemShader.mData.mRootSignature))))
		return false;

	D3D12_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;

	D3D12_BLEND_DESC blend_desc = {};
	blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.VS.pShaderBytecode = vs_blob->GetBufferPointer();
	pipeline_state_desc.VS.BytecodeLength = vs_blob->GetBufferSize();
	pipeline_state_desc.PS.pShaderBytecode = ps_blob->GetBufferPointer();
	pipeline_state_desc.PS.BytecodeLength = ps_blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = ioSystemShader.mData.mRootSignature.Get();
	pipeline_state_desc.RasterizerState = rasterizer_desc;
	pipeline_state_desc.BlendState = blend_desc;
	pipeline_state_desc.DepthStencilState.DepthEnable = FALSE;
	pipeline_state_desc.DepthStencilState.StencilEnable = FALSE;
	pipeline_state_desc.SampleMask = UINT_MAX;
	pipeline_state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipeline_state_desc.NumRenderTargets = 1;
	pipeline_state_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipeline_state_desc.SampleDesc.Count = 1;

	if (FAILED(gDevice->CreateGraphicsPipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioSystemShader.mData.mPipelineState))))
		return false;

	std::wstring name = gToWString(inVSName) + L"_" + gToWString(inPSName);
	ioSystemShader.mData.mPipelineState->SetName(name.c_str());

	return true;
}

bool gCreateCSPipelineState(const char* inShaderFileName, const char* inCSName, Shader& ioSystemShader)
{
	IDxcBlob* blob = gCompileShader(inShaderFileName, inCSName, "cs_6_6");
	if (blob == nullptr)
		return false;

	LPVOID root_signature_pointer = blob->GetBufferPointer();
	SIZE_T root_signature_size = blob->GetBufferSize();

	if (FAILED(gDevice->CreateRootSignature(0, root_signature_pointer, root_signature_size, IID_PPV_ARGS(&ioSystemShader.mData.mRootSignature))))
		return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.CS.pShaderBytecode = blob->GetBufferPointer();
	pipeline_state_desc.CS.BytecodeLength = blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = ioSystemShader.mData.mRootSignature.Get();
	if (FAILED(gDevice->CreateComputePipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioSystemShader.mData.mPipelineState))))
		return false;

	std::wstring name = gToWString(inCSName);
	ioSystemShader.mData.mPipelineState->SetName(name.c_str());

	return true;
}

bool gCreateLibPipelineState(const char* inShaderFileName, const char* inLibName, Shader& ioSystemShader)
{
	IDxcBlob* blob = gCompileShader(inShaderFileName, inLibName, "lib_6_6");
	if (blob == nullptr)
		return false;

	if (ioSystemShader.mLibRootSignatureReference == nullptr || ioSystemShader.mLibRootSignatureReference->mData.mRootSignature == nullptr)
		return false;

	std::vector<const wchar_t*> hit_shader_names;
	ComPtr<ID3D12StateObject> pipeline_object = gCreateLibStateObject(ioSystemShader.mLibType, blob, ioSystemShader.mLibRootSignatureReference->mData.mRootSignature.Get());
	if (pipeline_object == nullptr)
		return false;

	ioSystemShader.mData.mRootSignature = ioSystemShader.mLibRootSignatureReference->mData.mRootSignature;
	ioSystemShader.mData.mStateObject = pipeline_object;

	std::wstring name = gToWString(inLibName);
	ioSystemShader.mData.mStateObject->SetName(name.c_str());

	return true;
}

bool gCreatePipelineState(Shader& ioSystemShader)
{
	if (ioSystemShader.mLibName != nullptr)
		return gCreateLibPipelineState(ioSystemShader.mFileName, ioSystemShader.mLibName, ioSystemShader);
	else if (ioSystemShader.mCSName != nullptr)
		return gCreateCSPipelineState(ioSystemShader.mFileName, ioSystemShader.mCSName, ioSystemShader);
	else
		return gCreateVSPSPipelineState(ioSystemShader.mFileName, ioSystemShader.mVSName, ioSystemShader.mPSName, ioSystemShader);
}