#include "AccelerationStructure.h"
#include "Common.h"

#include <fstream>
#include <sstream>

IDxcBlob* CompileShader(const wchar_t* inName, const char* inSource, uint32_t inSize)
{
	static DxcCreateInstanceProc sDxcCreateInstanceProc = nullptr;
	if (sDxcCreateInstanceProc == nullptr)
	{
		HMODULE dll = LoadLibraryW(L"dxcompiler.dll");
		assert(dll != nullptr);
		sDxcCreateInstanceProc = (DxcCreateInstanceProc)GetProcAddress(dll, "DxcCreateInstance");
	}

	IDxcLibrary* library;
	IDxcBlobEncoding* blob_encoding;
	sDxcCreateInstanceProc(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void**)& library);
	gValidate(library->CreateBlobWithEncodingFromPinned(inSource, inSize, CP_UTF8, &blob_encoding));

	IDxcCompiler* compiler;
	sDxcCreateInstanceProc(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void**)& compiler);

	IDxcOperationResult* operation_result;
	assert(SUCCEEDED(compiler->Compile(
		blob_encoding,		// program text
		inName,				// file name, mostly for error messages
		L"",				// entry point function
		L"lib_6_3",			// target profile
		nullptr, 0,			// compilation arguments and their count
		nullptr, 0,			// name/value defines and their count
		nullptr,			// handler for #include directives
		&operation_result)));

	HRESULT compile_result;
	gValidate(operation_result->GetStatus(&compile_result));

	if (FAILED(compile_result))
	{
		IDxcBlobEncoding* pPrintBlob, * pPrintBlob16;
		gValidate(operation_result->GetErrorBuffer(&pPrintBlob));
		// We can use the library to get our preferred encoding.
		gValidate(library->GetBlobAsUtf16(pPrintBlob, &pPrintBlob16));
		wprintf(L"%*s", (int)pPrintBlob16->GetBufferSize() / 2, (LPCWSTR)pPrintBlob16->GetBufferPointer());
		pPrintBlob->Release();
		pPrintBlob16->Release();
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
};

void GenerateRayGenLocalRootDesc(RootSignatureDescriptor & outDesc)
{
	// RaytracingOutput - DescriptorRange
	D3D12_DESCRIPTOR_RANGE descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 0;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// RaytracingScene - DescriptorRange
	descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 1;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// RaytracingScene - DescriptorRange
	descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 2;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// RootDescriptor contains entry of DescriptorTable, DescriptorRange within
	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)outDesc.mDescriptorRanges.size();
	root_parameter.DescriptorTable.pDescriptorRanges = outDesc.mDescriptorRanges.data();
	outDesc.mRootParameters.push_back(root_parameter);

	// Create the RootDescriptor
	outDesc.mDesc.NumParameters = (UINT)outDesc.mRootParameters.size();
	outDesc.mDesc.pParameters = outDesc.mRootParameters.data();
	outDesc.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
}

void GenerateMissRootDesc(RootSignatureDescriptor& outDesc)
{
	// PerFrame - DescriptorRange
	D3D12_DESCRIPTOR_RANGE descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 2;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// RootDescriptor contains entry of DescriptorTable, DescriptorRange within
	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)outDesc.mDescriptorRanges.size();
	root_parameter.DescriptorTable.pDescriptorRanges = outDesc.mDescriptorRanges.data();
	outDesc.mRootParameters.push_back(root_parameter);

	// Create the RootDescriptor
	outDesc.mDesc.NumParameters = (UINT)outDesc.mRootParameters.size();
	outDesc.mDesc.pParameters = outDesc.mRootParameters.data();
	outDesc.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
}

void GenerateTriangleHitLocalRootDesc(RootSignatureDescriptor& outDesc)
{
	// PerScene - Descriptor - RootDescriptor contains descriptor directly
	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	root_parameter.Descriptor.ShaderRegister = 1;
	root_parameter.Descriptor.RegisterSpace = 0;
	outDesc.mRootParameters.push_back(root_parameter);

	// Create the RootDescriptor
	outDesc.mDesc.NumParameters = (UINT)outDesc.mRootParameters.size();
	outDesc.mDesc.pParameters = outDesc.mRootParameters.data();
	outDesc.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
}

void GeneratePlaneHitLocalRootDesc(RootSignatureDescriptor& outDesc)
{
	// RaytracingScene - DescriptorRange
	D3D12_DESCRIPTOR_RANGE descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 0;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// RootDescriptor contains entry of DescriptorTable, DescriptorRange within
	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)outDesc.mDescriptorRanges.size();
	root_parameter.DescriptorTable.pDescriptorRanges = outDesc.mDescriptorRanges.data();
	outDesc.mRootParameters.push_back(root_parameter);

	// Create the RootDescriptor
	outDesc.mDesc.NumParameters = (UINT)outDesc.mRootParameters.size();
	outDesc.mDesc.pParameters = outDesc.mRootParameters.data();
	outDesc.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
}

ComPtr<ID3D12RootSignature> CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC & desc)
{
	ComPtr<ID3DBlob> signature_blob;
	ComPtr<ID3DBlob> error_blob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
	if (FAILED(hr))
	{
		std::string str((char*)error_blob->GetBufferPointer(), error_blob->GetBufferSize());
		OutputDebugStringA(str.c_str());
		assert(false);
		return nullptr;
	}
	ComPtr<ID3D12RootSignature> root_signature;
	assert(SUCCEEDED(gDevice->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature))));
	return root_signature;
}

// Hold data for D3D12_STATE_SUBOBJECT
template <typename DescType, D3D12_STATE_SUBOBJECT_TYPE SubObjecType>
struct StateSubobjectHolder
{
	StateSubobjectHolder()
	{
		mStateSubobject.Type = SubObjecType;
		mStateSubobject.pDesc = &mDesc;
	}

	D3D12_STATE_SUBOBJECT mStateSubobject = {};

protected:
	DescType mDesc = {};
};

// Shader binary
struct DXILLibrary : public StateSubobjectHolder<D3D12_DXIL_LIBRARY_DESC, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY>
{
	DXILLibrary(IDxcBlob* inShaderBlob, const wchar_t* inEntryPoint[], uint32_t inEntryPointCount) : mShaderBlob(inShaderBlob)
	{
		mExportDescs.resize(inEntryPointCount);
		mExportNames.resize(inEntryPointCount);

		if (inShaderBlob)
		{
			mDesc.DXILLibrary.pShaderBytecode = inShaderBlob->GetBufferPointer();
			mDesc.DXILLibrary.BytecodeLength = inShaderBlob->GetBufferSize();
			mDesc.NumExports = inEntryPointCount;
			mDesc.pExports = mExportDescs.data();

			for (uint32_t i = 0; i < inEntryPointCount; i++)
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
		mDesc = mRootSignature.Get();
	}
private:
	ComPtr<ID3D12RootSignature> mRootSignature; // Necessary?
};

// Associate subobject to exports - Shader entry point -> Shader input
struct SubobjectToExportsAssociation : public StateSubobjectHolder<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION>
{
	SubobjectToExportsAssociation(const wchar_t* inExportNames[], uint32_t inExportCount, const D3D12_STATE_SUBOBJECT* inSubobjectToAssociate)
	{
		mDesc.NumExports = inExportCount;
		mDesc.pExports = inExportNames;
		mDesc.pSubobjectToAssociate = inSubobjectToAssociate;
	}
};

// Shader config
struct ShaderConfig : public StateSubobjectHolder<D3D12_RAYTRACING_SHADER_CONFIG, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG>
{
	ShaderConfig(uint32_t inMaxAttributeSizeInBytes, uint32_t inMaxPayloadSizeInBytes)
	{
		mDesc.MaxAttributeSizeInBytes = inMaxAttributeSizeInBytes;
		mDesc.MaxPayloadSizeInBytes = inMaxPayloadSizeInBytes;
	}
};

// Pipeline config
struct PipelineConfig : public StateSubobjectHolder<D3D12_RAYTRACING_PIPELINE_CONFIG, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG>
{
	PipelineConfig(uint32_t inMaxTraceRecursionDepth)
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
		mDesc = mRootSignature.Get();
	}
	ComPtr<ID3D12RootSignature> mRootSignature; // Necessary?
};

void gCreatePipelineState()
{
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
	uint32_t index = 0;

	// Load shader
	std::ifstream shader_file("Shader/Shader.hlsl");
	std::stringstream shader_stream;
	shader_stream << shader_file.rdbuf();

	// DXIL library
	const wchar_t* entry_points[] = { kRayGenShader, kMissShader, kTriangleHitShader, kPlaneHitShader, kShadowMissShader, kShadowHitShader };
	DXILLibrary dxilLibrary(CompileShader(L"Shader", shader_stream.str().c_str(), (uint32_t)shader_stream.str().length()), entry_points, ARRAYSIZE(entry_points));
	subobjects[index++] = dxilLibrary.mStateSubobject;

	// Hit group
	HitGroup triangle_hit_group(nullptr, kTriangleHitShader, kTriangleHitGroup);
	subobjects[index++] = triangle_hit_group.mStateSubobject;
	HitGroup plane_hit_group(nullptr, kPlaneHitShader, kPlaneHitGroup);
	subobjects[index++] = plane_hit_group.mStateSubobject;
	HitGroup shadow_hit_group(nullptr, kShadowHitShader, kShadowHitGroup);
	subobjects[index++] = shadow_hit_group.mStateSubobject;

	// Local root signatures and association
	// Ray-gen shader
	RootSignatureDescriptor ray_gen_local_root_signature_desc;
	GenerateRayGenLocalRootDesc(ray_gen_local_root_signature_desc);
	LocalRootSignature ray_gen_local_root_signature(ray_gen_local_root_signature_desc.mDesc);
	subobjects[index++] = ray_gen_local_root_signature.mStateSubobject;
	SubobjectToExportsAssociation ray_gen_association(&kRayGenShader, 1, &(subobjects[index - 1]));
	subobjects[index++] = ray_gen_association.mStateSubobject;

	// Miss shader
	RootSignatureDescriptor miss_local_root_signature_desc;
	GenerateMissRootDesc(miss_local_root_signature_desc);
	LocalRootSignature miss_local_root_signature(miss_local_root_signature_desc.mDesc);
	subobjects[index++] = miss_local_root_signature.mStateSubobject;
	SubobjectToExportsAssociation miss_association(&kMissShader, 1, &(subobjects[index - 1]));
	subobjects[index++] = miss_association.mStateSubobject;

	// Triangle hit shader
	RootSignatureDescriptor triangle_hit_local_root_signature_desc;
	GenerateTriangleHitLocalRootDesc(triangle_hit_local_root_signature_desc);
	LocalRootSignature triangle_hit_local_root_signature(triangle_hit_local_root_signature_desc.mDesc);
	subobjects[index++] = triangle_hit_local_root_signature.mStateSubobject;
	SubobjectToExportsAssociation triangle_hit_association(&kTriangleHitShader, 1, &(subobjects[index - 1]));
	subobjects[index++] = triangle_hit_association.mStateSubobject;

	// Plane hit shader
	RootSignatureDescriptor plane_hit_local_root_signature_desc;
	GeneratePlaneHitLocalRootDesc(plane_hit_local_root_signature_desc);
	LocalRootSignature plane_hit_local_root_signature(plane_hit_local_root_signature_desc.mDesc);
	subobjects[index++] = plane_hit_local_root_signature.mStateSubobject;
	SubobjectToExportsAssociation plane_hit_association(&kPlaneHitShader, 1, &(subobjects[index - 1]));
	subobjects[index++] = plane_hit_association.mStateSubobject;

#if 0 // Not really needed
	// Empty local root signature
	RootSignatureDescriptor empty_local_root_signature_descriptor;
	empty_local_root_signature_descriptor.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	LocalRootSignature empty_root_signature(empty_local_root_signature_descriptor.mDesc);
	subobjects[index++] = empty_root_signature.mStateSubobject;
	const wchar_t* shader_with_empty_associations[] = { kShadowMissShader, kShadowHitShader };
	SubobjectToExportsAssociation empty_local_root_signature_association(shader_with_empty_associations, ARRAYSIZE(shader_with_empty_associations), &(subobjects[index - 1]));
	subobjects[index++] = empty_local_root_signature_association.mStateSubobject;
#endif

	// Shader config
	//  sizeof(BuiltInTriangleIntersectionAttributes), depends on interaction type
	//  sizeof(RayPayload), fully customized
	ShaderConfig shader_config(sizeof(float) * 2, sizeof(float) * 3);
	subobjects[index++] = shader_config.mStateSubobject;
	const wchar_t* shader_exports[] = { kRayGenShader, kMissShader, kTriangleHitShader, kPlaneHitShader, kShadowHitShader, kShadowMissShader };
	SubobjectToExportsAssociation shader_configassociation(shader_exports, ARRAYSIZE(shader_exports), &(subobjects[index - 1]));
	subobjects[index++] = shader_configassociation.mStateSubobject;

	// Pipeline config
	//  MaxTraceRecursionDepth
	PipelineConfig pipeline_config(2); // Primary, Shadow
	subobjects[index++] = pipeline_config.mStateSubobject;

	// Global root signature
	GlobalRootSignature global_root_signature({});
	gDxrEmptyRootSignature = global_root_signature.mRootSignature.Get();
	subobjects[index++] = global_root_signature.mStateSubobject;

	// Create the state object
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

	// Most validation occurs here
	// Be sure use correct dll for dxc compiler
	// e.g. Error "Hash check failed for DXILibrary.pShaderBytecode" appears when dxil.dll is missing
	gValidate(gDevice->CreateStateObject(&desc, IID_PPV_ARGS(&gDxrStateObject)));
}

void gCleanupPipelineState()
{
	gSafeRelease(gDxrStateObject);
}