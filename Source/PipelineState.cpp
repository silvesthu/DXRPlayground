#include "AccelerationStructure.h"
#include "Common.h"

static const char kShaderSource[] = R"(
/***************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/
RaytracingAccelerationStructure RaytracingScene : register(t0);
RWTexture2D<float4> RaytracingOutput : register(u0);
cbuffer PerFrame : register(b0)
{
    float3 BackgroundColor;
}

float3 linearToSrgb(float3 c)
{
    // Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
    return srgb;
}

struct RayPayload
{
    float3 color;
};

[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    float2 crd = float2(launchIndex.xy);
    float2 dims = float2(launchDim.xy);

    float2 d = ((crd/dims) * 2.f - 1.f);
    float aspectRatio = dims.x / dims.y;

    RayDesc ray;
    ray.Origin = float3(0, 0, -2);
    ray.Direction = normalize(float3(d.x * aspectRatio, -d.y, 1));

    ray.TMin = 0;
    ray.TMax = 100000;

    RayPayload payload;
    TraceRay( RaytracingScene, 0 /*rayFlags*/, 0xFF, 0 /* ray index*/, 0, 0, ray, payload );
    float3 col = linearToSrgb(payload.color);
    RaytracingOutput[launchIndex.xy] = float4(col, 1);
}

[shader("miss")]
void miss(inout RayPayload payload)
{
    payload.color = BackgroundColor;
}

[shader("closesthit")]
void chs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);

    const float3 A = float3(1, 0, 0);
    const float3 B = float3(0, 1, 0);
    const float3 C = float3(0, 0, 1);

    payload.color = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;
}
)";

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
	// RaytracingOutput
	D3D12_DESCRIPTOR_RANGE descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 0;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// RaytracingScene
	descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 1;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// Root Parameters
	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)outDesc.mDescriptorRanges.size();
	root_parameter.DescriptorTable.pDescriptorRanges = outDesc.mDescriptorRanges.data();
	outDesc.mRootParameters.push_back(root_parameter);

	// Create the desc
	outDesc.mDesc.NumParameters = (UINT)outDesc.mRootParameters.size();
	outDesc.mDesc.pParameters = outDesc.mRootParameters.data();
	outDesc.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
}

void GenerateMissClosestHitRootDesc(RootSignatureDescriptor& outDesc)
{
	// PerFrame
	D3D12_DESCRIPTOR_RANGE descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 2;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// Root Parameters
	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)outDesc.mDescriptorRanges.size();
	root_parameter.DescriptorTable.pDescriptorRanges = outDesc.mDescriptorRanges.data();
	outDesc.mRootParameters.push_back(root_parameter);

	// Create the desc
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
	assert(SUCCEEDED(gD3DDevice->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature))));
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

void CreatePipelineState()
{
	// See D3D12_STATE_SUBOBJECT_TYPE
	// Notice all pointers should be valid until CreateStateObject

	// Need 10 subobjects:
	//  1 for the DXIL library
	//  1 for hit-group
	//  2 for RayGen root-signature (root-signature and the subobject association)
	//  2 for the root-signature shared between miss and hit shaders (signature and association)
	//  2 for shader config (shared between all programs. 1 for the config, 1 for association)
	//  1 for pipeline config
	//  1 for the global root signature
	std::array<D3D12_STATE_SUBOBJECT, 10> subobjects;
	uint32_t index = 0;

	// 1 for the DXIL library
	const wchar_t* entry_points[] = { kRayGenShader, kMissShader, kClosestHitShader };
	DXILLibrary dxilLibrary(CompileShader(L"Shader", kShaderSource, ARRAYSIZE(kShaderSource)), entry_points, ARRAYSIZE(entry_points));
	subobjects[index++] = dxilLibrary.mStateSubobject;

	// 1 for hit-group
	HitGroup hit_group(nullptr, kClosestHitShader, kHitGroup);
	subobjects[index++] = hit_group.mStateSubobject;

	// 2 for RayGen root-signature
	RootSignatureDescriptor ray_gen_local_root_signature_desc;
	GenerateRayGenLocalRootDesc(ray_gen_local_root_signature_desc);
	LocalRootSignature ray_gen_local_root_signature(ray_gen_local_root_signature_desc.mDesc);
	subobjects[index++] = ray_gen_local_root_signature.mStateSubobject;

	SubobjectToExportsAssociation ray_gen_association(&kRayGenShader, 1, &(subobjects[index - 1]));
	subobjects[index++] = ray_gen_association.mStateSubobject;

	// 2 for the root-signature shared between miss and hit shaders
	RootSignatureDescriptor miss_closest_hit_local_root_signature_desc;
	GenerateMissClosestHitRootDesc(miss_closest_hit_local_root_signature_desc);
	LocalRootSignature miss_closest_hit_local_root_signature(miss_closest_hit_local_root_signature_desc.mDesc);
	subobjects[index++] = miss_closest_hit_local_root_signature.mStateSubobject;

	const wchar_t* miss_closest_hit_export_names[] = { kMissShader, kClosestHitShader };
	SubobjectToExportsAssociation miss_closest_hit_association(miss_closest_hit_export_names, ARRAYSIZE(miss_closest_hit_export_names), &(subobjects[index - 1]));
	subobjects[index++] = miss_closest_hit_association.mStateSubobject;

	// 2 for shader config
	ShaderConfig shader_config(sizeof(float) * 2, sizeof(float) * 3); // ???, see struct RayPayload
	subobjects[index++] = shader_config.mStateSubobject;

	const wchar_t* shader_exports[] = { kMissShader, kClosestHitShader, kRayGenShader }; // does order matter?
	SubobjectToExportsAssociation shader_configassociation(shader_exports, ARRAYSIZE(shader_exports), &(subobjects[index - 1]));
	subobjects[index++] = shader_configassociation.mStateSubobject;

	// 1 for pipeline config
	PipelineConfig pipeline_config(1); // ???
	subobjects[index++] = pipeline_config.mStateSubobject;

	// 1 for the global root signature
	GlobalRootSignature global_root_signature({});
	gDxrEmptyRootSignature = global_root_signature.mRootSignature.Get();
	subobjects[index++] = global_root_signature.mStateSubobject;

	// Create the state
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = (UINT)subobjects.size();
	desc.pSubobjects = subobjects.data();
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

	// Most validation occurs here
	// Be sure use correct dll for dxc compiler
	// e.g. Error "Hash check failed for DXILibrary.pShaderBytecode" appears when dxil.dll is missing.
	gValidate(gD3DDevice->CreateStateObject(&desc, IID_PPV_ARGS(&gDxrStateObject)));
}

void CleanupPipelineState()
{
	gSafeRelease(gDxrStateObject);
}