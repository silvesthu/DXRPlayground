#include "Common.h"
#include "Atmosphere.h"
#include "Cloud.h"

#include <fstream>
#include <sstream>

static IDxcBlob* sCompileShader(const char* inFilename, const char* inEntryPoint, const char* inProfile)
{
	// Load shader
	std::ifstream shader_file(inFilename);
	std::stringstream shader_stream;
	shader_stream << shader_file.rdbuf();
	std::string shader_string = shader_stream.str();

	// GetProcAddress to eliminate dependency on .lib. Make updating .dll easier.
	DxcCreateInstanceProc DxcCreateInstance = nullptr;
	if (DxcCreateInstance == nullptr)
	{
		HMODULE dll = LoadLibraryW(L"dxcompiler.dll");
		assert(dll != nullptr);
		DxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dll, "DxcCreateInstance"));
	}

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
	DxcDefine dxc_define_profile {};
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
	DxcDefine dxc_define_entry_point {};
	dxc_define_entry_point.Name = entry_point_macro.c_str();
	defines.push_back(dxc_define_entry_point);

	std::vector<LPCWSTR> arguments;
	arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);				// -WX
	arguments.push_back(DXC_ARG_ALL_RESOURCES_BOUND);				// -all_resources_bound
	arguments.push_back(DXC_ARG_DEBUG);								// -Zi
	arguments.push_back(L"-Qembed_debug");							// -Qembed_debug

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
		std::string str(static_cast<LPCSTR>(blob_8->GetBufferPointer()), blob_8->GetBufferSize());
		str += '\n';
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
		std::string str(static_cast<LPCSTR>(blob_8->GetBufferPointer()), blob_8->GetBufferSize());

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

ComPtr<ID3D12RootSignature> CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC & desc)
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
	LocalRootSignature(const D3D12_ROOT_SIGNATURE_DESC& inDesc)
	{
		mRootSignature = CreateRootSignature(inDesc);
		mRootSignature->SetName(L"LocalRootSignature");
		mDesc = mRootSignature.Get();
	}
private:
	ComPtr<ID3D12RootSignature> mRootSignature; // Necessary?
};

// Associate subobject to exports - Shader entry point -> Shader input
struct SubobjectToExportsAssociation : public StateSubobjectHolder<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION>
{
	SubobjectToExportsAssociation(const wchar_t* inExportNames[], glm::uint32 inExportCount, const D3D12_STATE_SUBOBJECT* inSubobjectToAssociate)
	{
		mDesc.NumExports = inExportCount;
		mDesc.pExports = inExportNames;
		mDesc.pSubobjectToAssociate = inSubobjectToAssociate;
	}
};

// Shader config
struct ShaderConfig : public StateSubobjectHolder<D3D12_RAYTRACING_SHADER_CONFIG, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG>
{
	ShaderConfig(glm::uint32 inMaxAttributeSizeInBytes, glm::uint32 inMaxPayloadSizeInBytes)
	{
		mDesc.MaxAttributeSizeInBytes = inMaxAttributeSizeInBytes;
		mDesc.MaxPayloadSizeInBytes = inMaxPayloadSizeInBytes;
	}
};

// Pipeline config
struct PipelineConfig : public StateSubobjectHolder<D3D12_RAYTRACING_PIPELINE_CONFIG, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG>
{
	PipelineConfig(glm::uint32 inMaxTraceRecursionDepth)
	{
		mDesc.MaxTraceRecursionDepth = inMaxTraceRecursionDepth;
	}
};

// Global root signature
struct GlobalRootSignature : public StateSubobjectHolder<ID3D12RootSignature*, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE>
{
	GlobalRootSignature(const ComPtr<ID3D12RootSignature>& inRootSignature)
	{
		mDesc = inRootSignature.Get();
	}
};

static bool sCreateVSPSPipelineState(const char* inShaderFileName, const char* inVSName, const char* inPSName, Shader& ioSystemShader)
{
	IDxcBlob* vs_blob = sCompileShader(inShaderFileName, inVSName, "vs_6_6");
	IDxcBlob* ps_blob = sCompileShader(inShaderFileName, inPSName, "ps_6_6");
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

	return true;
}

static bool sCreateCSPipelineState(const char* inShaderFileName, const char* inCSName, Shader& ioSystemShader)
{
	IDxcBlob* cs_blob = sCompileShader(inShaderFileName, inCSName, "cs_6_6");
	if (cs_blob == nullptr)
		return false;

	LPVOID root_signature_pointer = cs_blob->GetBufferPointer();
	SIZE_T root_signature_size = cs_blob->GetBufferSize();

	if (FAILED(gDevice->CreateRootSignature(0, root_signature_pointer, root_signature_size, IID_PPV_ARGS(&ioSystemShader.mData.mRootSignature))))
		return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.CS.pShaderBytecode = cs_blob->GetBufferPointer();
	pipeline_state_desc.CS.BytecodeLength = cs_blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = ioSystemShader.mData.mRootSignature.Get();
	if (FAILED(gDevice->CreateComputePipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioSystemShader.mData.mPipelineState))))
		return false;

	return true;
}

static bool sCreatePipelineState(Shader& ioSystemShader)
{
	if (ioSystemShader.mCSName != nullptr)
		return sCreateCSPipelineState(ioSystemShader.mFileName, ioSystemShader.mCSName, ioSystemShader);
	else
		return sCreateVSPSPipelineState(ioSystemShader.mFileName, ioSystemShader.mVSName, ioSystemShader.mPSName, ioSystemShader);
}

// From https://github.com/microsoft/DirectX-Graphics-Samples/blob/e5ea2ac7430ce39e6f6d619fd85ae32581931589/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingHelloWorld/DirectXRaytracingHelper.h#L172
static void sPrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
{
	std::wstringstream wstr;
	wstr << L"\n";
	wstr << L"--------------------------------------------------------------------\n";
	wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
	if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
	if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

	auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
	{
		std::wostringstream woss;
		for (UINT i = 0; i < numExports; i++)
		{
			woss << L"|";
			if (depth > 0)
			{
				for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
			}
			woss << L" [" << i << L"]: ";
			if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
			woss << exports[i].Name << L"\n";
		}
		return woss.str();
	};

	for (UINT i = 0; i < desc->NumSubobjects; i++)
	{
		wstr << L"| [" << i << L"]: ";
		switch (desc->pSubobjects[i].Type)
		{
		case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
			wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
			wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
			wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
		{
			wstr << L"DXIL Library 0x";
			auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
			wstr << ExportTree(1, lib->NumExports, lib->pExports);
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
		{
			wstr << L"Existing Library 0x";
			auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << collection->pExistingCollection << L"\n";
			wstr << ExportTree(1, collection->NumExports, collection->pExports);
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
		{
			wstr << L"Subobject to Exports Association (Subobject [";
			auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
			UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
			wstr << index << L"])\n";
			for (UINT j = 0; j < association->NumExports; j++)
			{
				wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
			}
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
		{
			wstr << L"DXIL Subobjects to Exports Association (";
			auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
			wstr << association->SubobjectToAssociate << L")\n";
			for (UINT j = 0; j < association->NumExports; j++)
			{
				wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
			}
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
		{
			wstr << L"Raytracing Shader Config\n";
			auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
			wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
			wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
		{
			wstr << L"Raytracing Pipeline Config\n";
			auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
			wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
		{
			wstr << L"Hit Group (";
			auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
			wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
			wstr << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
			wstr << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
			break;
		}
		}
		wstr << L"|--------------------------------------------------------------------\n";
	}
	wstr << L"\n";
	OutputDebugStringW(wstr.str().c_str());
}

void gCreatePipelineState()
{
	// Create non-lib shaders
	{
		bool succeed = true;

		succeed &= sCreatePipelineState(gRenderer.mRuntime.mCompositeShader);
		succeed &= sCreatePipelineState(gRenderer.mRuntime.mRayQueryShader);
		succeed &= sCreatePipelineState(gRenderer.mRuntime.mDiffTexture2DShader);
		succeed &= sCreatePipelineState(gRenderer.mRuntime.mDiffTexture3DShader);

		for (auto&& shaders : gAtmosphere.mRuntime.mShadersSet)
			for (auto&& shader : shaders)
				succeed &= sCreatePipelineState(shader);

		for (auto&& shader : gCloud.mRuntime.mShaders)
			succeed &= sCreatePipelineState(shader);
	}

	// Create lib shaders
	const wchar_t* entry_points[] = { kDefaultRayGenerationShader, kDefaultMissShader, kDefaultClosestHitShader };
	IDxcBlob* blob = sCompileShader("Shader/Raytracing.hlsl", "", "lib_6_6");
	if (blob == nullptr)
	{
		assert(gDXRStateObject != nullptr);
		return;
	}

	// See D3D12_STATE_SUBOBJECT_TYPE
	// https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#d3d12_state_subobject_type
	// Note that all pointers should be valid until CreateStateObject

	// Subobjects:
	//  DXIL library
	//  Hit group
	//  Local root signature and association
	//  Shader config
	//  Pipeline config
	//  Global root signature

	// When add new shader:
	//  DXIL library
	//  Hit group
	//  Local root signature and association
	//  Shader config

	std::array<D3D12_STATE_SUBOBJECT, 64> subobjects;
	glm::uint32 index = 0;

	DXILLibrary dxilLibrary(blob, entry_points, ARRAYSIZE(entry_points));
	subobjects[index++] = dxilLibrary.mStateSubobject;

	// Hit group
	HitGroup default_hit_group(nullptr, kDefaultClosestHitShader, kDefaultHitGroup);
	subobjects[index++] = default_hit_group.mStateSubobject;

	// Local root signatures and association (not necessary here)
	D3D12_ROOT_SIGNATURE_DESC local_signature_desc = {};
	local_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	LocalRootSignature local_root_signature(local_signature_desc);
	subobjects[index++] = local_root_signature.mStateSubobject;
	SubobjectToExportsAssociation export_association(entry_points, ARRAYSIZE(entry_points), &(subobjects[index - 1]));
	subobjects[index++] = export_association.mStateSubobject;

	// Shader config
	//  sizeof(BuiltInTriangleIntersectionAttributes) = sizeof(float) * 2, depends on interaction type
	//  sizeof(Payload), fully customized
	ShaderConfig shader_config(sizeof(float) * 2, (glm::uint32)sizeof(RayPayload));
	subobjects[index++] = shader_config.mStateSubobject;

	// Pipeline config
	//  MaxTraceRecursionDepth
	PipelineConfig pipeline_config(31); // [0, 31] https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_raytracing_pipeline_config
	subobjects[index++] = pipeline_config.mStateSubobject;

	// Global root signature - grab from RayQuery version
	gDXRGlobalRootSignature = gRenderer.mRuntime.mRayQueryShader.mData.mRootSignature;
	GlobalRootSignature global_root_signature(gDXRGlobalRootSignature);
	subobjects[index++] = global_root_signature.mStateSubobject;

	// Create the state object
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	gValidate(gDevice->CreateStateObject(&desc, IID_PPV_ARGS(&gDXRStateObject)));

	if (gRenderer.mPrintStateObjectDesc)
		sPrintStateObjectDesc(&desc);
}

void gCleanupPipelineState()
{
	gDXRStateObject = nullptr;
	gDXRGlobalRootSignature = nullptr;
}