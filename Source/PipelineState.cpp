#include "Common.h"
#include "Atmosphere.h"
#include "Cloud.h"

#include <fstream>
#include <sstream>

static IDxcBlob* sCompileShader(const char* inFilename, const wchar_t* inEntryPoint, const wchar_t* inProfile)
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
	std::wstring_view profile(inProfile);
	DxcDefine dxc_define_profile {};
	dxc_define_profile.Name = L"SHADER_PROFILE_UNKNOWN";
	dxc_define_profile.Value = L"1";
	if (profile._Starts_with(L"lib"))
		dxc_define_profile.Name = L"SHADER_PROFILE_LIB";
	if (profile._Starts_with(L"cs"))
		dxc_define_profile.Name = L"SHADER_PROFILE_CS";
	if (profile._Starts_with(L"ps"))
		dxc_define_profile.Name = L"SHADER_PROFILE_PS";
	if (profile._Starts_with(L"VS"))
		dxc_define_profile.Name = L"SHADER_PROFILE_VS";
	defines.push_back(dxc_define_profile);

	std::wstring entry_point_name = L"ENTRY_POINT_";
	entry_point_name += inEntryPoint;
	DxcDefine dxc_define_entry_point {};
	dxc_define_entry_point.Name = entry_point_name.c_str();
	defines.push_back(dxc_define_entry_point);

	std::vector<LPCWSTR> arguments;
	arguments.push_back(DXC_ARG_DEBUG);								// -Zi
	arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);				// -WX
	arguments.push_back(DXC_ARG_ALL_RESOURCES_BOUND);				// -all_resources_bound
	arguments.push_back(L"-Qembed_debug");							// -Qembed_debug

	IDxcOperationResult* operation_result;
	if (FAILED(compiler->Compile(
		blob_encoding,												// program text
		wfilename.c_str(),											// file name, mostly for error messages
		inEntryPoint,												// entry point function
		inProfile,													// target profile
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
		gDebugPrint(str.c_str());
		blob->Release();
		blob_8->Release();
		return nullptr;
	}

	IDxcBlob* blob = nullptr;
	gValidate(operation_result->GetResult(&blob));
	return blob;
}

struct RootSignatureDescriptor
{
	D3D12_ROOT_SIGNATURE_DESC mDesc = {};
	std::vector<D3D12_DESCRIPTOR_RANGE> mDescriptorRanges;
	std::vector<D3D12_ROOT_PARAMETER> mRootParameters;
	std::vector<D3D12_STATIC_SAMPLER_DESC> mStaticSamplerDescs;
};

void GenerateGlobalRootSignatureDescriptor(RootSignatureDescriptor& outDesc)
{
	// u0
	D3D12_DESCRIPTOR_RANGE descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// b0
	descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// t, space0
	descriptor_range = {};
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	for (int i = 0; i < 6; i++)
	{
		descriptor_range.BaseShaderRegister = i;
		outDesc.mDescriptorRanges.push_back(descriptor_range);
	}

	// b0, space2 - Atmosphere
	descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 2;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// t, space2 - Atmosphere
	descriptor_range = {};
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 2;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	for (int i = 0; i < gAtmosphere.mResource.mTextures.size(); i++)
	{
		descriptor_range.BaseShaderRegister = i;
		outDesc.mDescriptorRanges.push_back(descriptor_range);
	}

	// b, space3 - Cloud
	descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 3;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// t, space3 - Cloud
	descriptor_range = {};
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 3;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptor_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	for (int i = 0; i < gCloud.mResource.mTextures.size(); i++)
	{
		descriptor_range.BaseShaderRegister = i;
		outDesc.mDescriptorRanges.push_back(descriptor_range);
	}

	// Table
	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(outDesc.mDescriptorRanges.size());
	root_parameter.DescriptorTable.pDescriptorRanges = outDesc.mDescriptorRanges.data();
	outDesc.mRootParameters.push_back(root_parameter);

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.MipLODBias = 0.f;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	sampler.ShaderRegister = 0;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	outDesc.mStaticSamplerDescs.push_back(sampler);

	sampler.ShaderRegister = 1;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	outDesc.mStaticSamplerDescs.push_back(sampler);

	// Descriptor to table
	outDesc.mDesc.NumParameters = static_cast<UINT>(outDesc.mRootParameters.size());
	outDesc.mDesc.pParameters = outDesc.mRootParameters.data();
	outDesc.mDesc.NumStaticSamplers = static_cast<UINT>(outDesc.mStaticSamplerDescs.size());
	outDesc.mDesc.pStaticSamplers = outDesc.mStaticSamplerDescs.data();
	outDesc.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
}

ComPtr<ID3D12RootSignature> CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC & desc)
{
	ComPtr<ID3DBlob> signature_blob;
	ComPtr<ID3DBlob> error_blob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
	if (FAILED(hr))
	{
		std::string str((char*)error_blob->GetBufferPointer(), error_blob->GetBufferSize());
		gDebugPrint(str.c_str());
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
	GlobalRootSignature(const D3D12_ROOT_SIGNATURE_DESC& inDesc)
	{
		mRootSignature = CreateRootSignature(inDesc);
		mRootSignature->SetName(L"GlobalRootSignature");
		mDesc = mRootSignature.Get();
	}
	ComPtr<ID3D12RootSignature> mRootSignature;
};

static bool sCreateVSPSPipelineState(const char* inShaderFileName, const wchar_t* inVSName, const wchar_t* inPSName, Shader& ioSystemShader)
{
	IDxcBlob* vs_blob = sCompileShader(inShaderFileName, inVSName, L"vs_6_6");
	IDxcBlob* ps_blob = sCompileShader(inShaderFileName, inPSName, L"ps_6_6");
	if (vs_blob == nullptr || ps_blob == nullptr)
		return false;

	if (FAILED(gDevice->CreateRootSignature(0, ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), IID_PPV_ARGS(&ioSystemShader.mData.mRootSignature))))
		return false;

	ComPtr<ID3D12RootSignatureDeserializer> deserializer;
	if (FAILED(D3D12CreateRootSignatureDeserializer(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), IID_PPV_ARGS(&ioSystemShader.mData.mRootSignatureDeserializer))))
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

static bool sCreateCSPipelineState(const char* inShaderFileName, const wchar_t* inCSName, Shader& ioSystemShader)
{
	IDxcBlob* cs_blob = sCompileShader(inShaderFileName, inCSName, L"cs_6_6");
	if (cs_blob == nullptr)
		return false;

	LPVOID root_signature_pointer = cs_blob->GetBufferPointer();
	SIZE_T root_signature_size = cs_blob->GetBufferSize();

	if (ioSystemShader.mUseGlobalRootSignature)
		ioSystemShader.mData.mRootSignature = gDXRGlobalRootSignature;
	else
	{
		if (FAILED(gDevice->CreateRootSignature(0, root_signature_pointer, root_signature_size, IID_PPV_ARGS(&ioSystemShader.mData.mRootSignature))))
			return false;

		ComPtr<ID3D12RootSignatureDeserializer> deserializer;
		if (FAILED(D3D12CreateRootSignatureDeserializer(cs_blob->GetBufferPointer(), cs_blob->GetBufferSize(), IID_PPV_ARGS(&ioSystemShader.mData.mRootSignatureDeserializer))))
			return false;
	}

	D3D12_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;

	D3D12_BLEND_DESC blend_desc = {};
	blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.CS.pShaderBytecode = cs_blob->GetBufferPointer();
	pipeline_state_desc.CS.BytecodeLength = cs_blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = ioSystemShader.mData.mRootSignature.Get();

	if (FAILED(gDevice->CreateComputePipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioSystemShader.mData.mPipelineState))))
		return false;

	return true;
}

bool sCreatePipelineState(const char* inShaderFileName, Shader& ioSystemShader)
{
	if (ioSystemShader.mCSName != nullptr)
		return sCreateCSPipelineState(inShaderFileName, ioSystemShader.mCSName, ioSystemShader);
	else
		return sCreateVSPSPipelineState(inShaderFileName, ioSystemShader.mVSName, ioSystemShader.mPSName, ioSystemShader);
}

void gCreatePipelineState()
{
	static bool first_run = true;

	// Create lib shaders
	const wchar_t* entry_points[] = { kDefaultRayGenerationShader, kDefaultMissShader, kDefaultClosestHitShader, kShadowMissShader, kShadowClosestHitShader };
	IDxcBlob* blob = sCompileShader("Shader/Raytracing.hlsl", L"", L"lib_6_5");
	if (blob == nullptr)
	{
		if (first_run)
			assert(gDXRStateObject != nullptr);
		return;
	}

	// See D3D12_STATE_SUBOBJECT_TYPE
	// Note that all pointers should be valid until CreateStateObject
	
	// Subobjects:
	//  DXIL library
	//  Hit group
	//  Local root signature and association for each shader
	//  Shader config and association
	//  Pipeline config
	//  Global root signature

	// When add new shader:
	//  DXIL library (for shader binary)
	//  Hit group
	//  Local root signature and association
	//  Shader config

	std::array<D3D12_STATE_SUBOBJECT, 64> subobjects;
	glm::uint32 index = 0;

	gDebugPrint("Shader compiled.\n");
	DXILLibrary dxilLibrary(blob, entry_points, ARRAYSIZE(entry_points));
	subobjects[index++] = dxilLibrary.mStateSubobject;

	// Hit group
	HitGroup default_hit_group(nullptr, kDefaultClosestHitShader, kDefaultHitGroup);
	subobjects[index++] = default_hit_group.mStateSubobject;
	HitGroup shadow_hit_group(nullptr, kShadowClosestHitShader, kShadowHitGroup);
	subobjects[index++] = shadow_hit_group.mStateSubobject;

	// Local root signatures and association
	RootSignatureDescriptor local_root_signature_descriptor;
	local_root_signature_descriptor.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	LocalRootSignature local_root_signature(local_root_signature_descriptor.mDesc);
	subobjects[index++] = local_root_signature.mStateSubobject;
	SubobjectToExportsAssociation export_association(entry_points, ARRAYSIZE(entry_points), &(subobjects[index - 1]));
	subobjects[index++] = export_association.mStateSubobject;

	// Shader config
	//  sizeof(BuiltInTriangleIntersectionAttributes) = sizeof(float) * 2, depends on interaction type
	//  sizeof(Payload), fully customized
	ShaderConfig shader_config(sizeof(float) * 2, (glm::uint32)std::max(sizeof(RayPayload), sizeof(ShadowPayload)));
	subobjects[index++] = shader_config.mStateSubobject;
	SubobjectToExportsAssociation shader_config_association(entry_points, ARRAYSIZE(entry_points), &(subobjects[index - 1]));
	subobjects[index++] = shader_config_association.mStateSubobject;

	// Pipeline config
	//  MaxTraceRecursionDepth
	PipelineConfig pipeline_config(31); // [0, 31] https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_raytracing_pipeline_config
	subobjects[index++] = pipeline_config.mStateSubobject;

	// Global root signature
	RootSignatureDescriptor global_root_signature_descriptor;
	GenerateGlobalRootSignatureDescriptor(global_root_signature_descriptor);
	GlobalRootSignature global_root_signature(global_root_signature_descriptor.mDesc);
	gDXRGlobalRootSignature = global_root_signature.mRootSignature;
	subobjects[index++] = global_root_signature.mStateSubobject;

	// Create the state object
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	gValidate(gDevice->CreateStateObject(&desc, IID_PPV_ARGS(&gDXRStateObject)));

	// Create non-lib shaders
	{
		bool succeed = true;

		succeed &= sCreatePipelineState("Shader/Composite.hlsl", gCompositeShader);

		succeed &= sCreatePipelineState("Shader/Raytracing.hlsl", gDXRInlineShader);

		succeed &= sCreatePipelineState("Shader/DiffTexture.hlsl", gDiffTexture2DShader);
		succeed &= sCreatePipelineState("Shader/DiffTexture.hlsl", gDiffTexture3DShader);

		for (auto&& shader : gAtmosphere.mResource.mShaders)
			succeed &= sCreatePipelineState("Shader/Atmosphere.hlsl", shader);

		for (auto&& shader : gCloud.mResource.mShaders)
			succeed &= sCreatePipelineState("Shader/Cloud.hlsl", shader);

		if (!succeed)
		{
			if (first_run)
				assert(gDXRStateObject != nullptr);
			return;
		}
	}

	first_run = false; // disable assert for unsuccessful shader reload
}

void gCleanupPipelineState()
{
	gDXRStateObject = nullptr;
	gDXRGlobalRootSignature = nullptr;

	gDXRInlineShader.Reset();

	gDiffTexture2DShader.Reset();
	gDiffTexture3DShader.Reset();

	gCompositeShader.Reset();
}