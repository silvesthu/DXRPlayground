#include "Renderer.h"
#include "Atmosphere.h"
#include "Cloud.h"

namespace RendererHelper
{
	enum class D3D_SHADER_REQUIRES
	{
		REQUIRES_DOUBLES															= D3D_SHADER_REQUIRES_DOUBLES,
		REQUIRES_EARLY_DEPTH_STENCIL												= D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL,
		REQUIRES_UAVS_AT_EVERY_STAGE												= D3D_SHADER_REQUIRES_UAVS_AT_EVERY_STAGE,
		REQUIRES_64_UAVS															= D3D_SHADER_REQUIRES_64_UAVS,
		REQUIRES_MINIMUM_PRECISION													= D3D_SHADER_REQUIRES_MINIMUM_PRECISION,
		REQUIRES_11_1_DOUBLE_EXTENSIONS												= D3D_SHADER_REQUIRES_11_1_DOUBLE_EXTENSIONS,
		REQUIRES_11_1_SHADER_EXTENSIONS												= D3D_SHADER_REQUIRES_11_1_SHADER_EXTENSIONS,
		REQUIRES_LEVEL_9_COMPARISON_FILTERING										= D3D_SHADER_REQUIRES_LEVEL_9_COMPARISON_FILTERING,
		REQUIRES_TILED_RESOURCES													= D3D_SHADER_REQUIRES_TILED_RESOURCES,
		REQUIRES_STENCIL_REF														= D3D_SHADER_REQUIRES_STENCIL_REF,
		REQUIRES_INNER_COVERAGE														= D3D_SHADER_REQUIRES_INNER_COVERAGE,
		REQUIRES_TYPED_UAV_LOAD_ADDITIONAL_FORMATS									= D3D_SHADER_REQUIRES_TYPED_UAV_LOAD_ADDITIONAL_FORMATS,
		REQUIRES_ROVS																= D3D_SHADER_REQUIRES_ROVS,
		REQUIRES_VIEWPORT_AND_RT_ARRAY_INDEX_FROM_ANY_SHADER_FEEDING_RASTERIZER		= D3D_SHADER_REQUIRES_VIEWPORT_AND_RT_ARRAY_INDEX_FROM_ANY_SHADER_FEEDING_RASTERIZER,
		REQUIRES_WAVE_OPS															= D3D_SHADER_REQUIRES_WAVE_OPS,
		REQUIRES_INT64_OPS															= D3D_SHADER_REQUIRES_INT64_OPS,
		REQUIRES_VIEW_ID															= D3D_SHADER_REQUIRES_VIEW_ID,
		REQUIRES_BARYCENTRICS														= D3D_SHADER_REQUIRES_BARYCENTRICS,
		REQUIRES_NATIVE_16BIT_OPS													= D3D_SHADER_REQUIRES_NATIVE_16BIT_OPS,
		REQUIRES_SHADING_RATE														= D3D_SHADER_REQUIRES_SHADING_RATE,
		REQUIRES_RAYTRACING_TIER_1_1												= D3D_SHADER_REQUIRES_RAYTRACING_TIER_1_1,
		REQUIRES_SAMPLER_FEEDBACK													= D3D_SHADER_REQUIRES_SAMPLER_FEEDBACK,
		REQUIRES_ATOMIC_INT64_ON_TYPED_RESOURCE										= D3D_SHADER_REQUIRES_ATOMIC_INT64_ON_TYPED_RESOURCE,
		REQUIRES_ATOMIC_INT64_ON_GROUP_SHARED										= D3D_SHADER_REQUIRES_ATOMIC_INT64_ON_GROUP_SHARED,
		REQUIRES_DERIVATIVES_IN_MESH_AND_AMPLIFICATION_SHADERS						= D3D_SHADER_REQUIRES_DERIVATIVES_IN_MESH_AND_AMPLIFICATION_SHADERS,
		REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING									= D3D_SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING,
		REQUIRES_SAMPLER_DESCRIPTOR_HEAP_INDEXING									= D3D_SHADER_REQUIRES_SAMPLER_DESCRIPTOR_HEAP_INDEXING,
		REQUIRES_WAVE_MMA															= D3D_SHADER_REQUIRES_WAVE_MMA,
		REQUIRES_ATOMIC_INT64_ON_DESCRIPTOR_HEAP_RESOURCE							= D3D_SHADER_REQUIRES_ATOMIC_INT64_ON_DESCRIPTOR_HEAP_RESOURCE,
		FEATURE_ADVANCED_TEXTURE_OPS												= D3D_SHADER_FEATURE_ADVANCED_TEXTURE_OPS,
		FEATURE_WRITEABLE_MSAA_TEXTURES												= D3D_SHADER_FEATURE_WRITEABLE_MSAA_TEXTURES,
	};

	std::string ShaderDescToString(const D3D12_SHADER_DESC& inShaderDesc)
	{
		std::string string;

		string += "D3D12_SHADER_DESC\n";
		string += std::format("\t{:64} = {}\n", "Version", gToString(inShaderDesc.Version).c_str());
		string += std::format("\t{:64} = {}\n", "Creator", gToString(inShaderDesc.Creator).c_str());
		string += std::format("\t{:64} = {}\n", "Flags", gToString(inShaderDesc.Flags).c_str());
		string += std::format("\t{:64} = {}\n", "ConstantBuffers", gToString(inShaderDesc.ConstantBuffers).c_str());
		string += std::format("\t{:64} = {}\n", "BoundResources", gToString(inShaderDesc.BoundResources).c_str());
		string += std::format("\t{:64} = {}\n", "InputParameters", gToString(inShaderDesc.InputParameters).c_str());
		string += std::format("\t{:64} = {}\n", "OutputParameters", gToString(inShaderDesc.OutputParameters).c_str());
		string += std::format("\t{:64} = {}\n", "InstructionCount", gToString(inShaderDesc.InstructionCount).c_str());
		string += std::format("\t{:64} = {}\n", "TempRegisterCount", gToString(inShaderDesc.TempRegisterCount).c_str());
		string += std::format("\t{:64} = {}\n", "TempArrayCount", gToString(inShaderDesc.TempArrayCount).c_str());
		string += std::format("\t{:64} = {}\n", "DefCount", gToString(inShaderDesc.DefCount).c_str());
		string += std::format("\t{:64} = {}\n", "DclCount", gToString(inShaderDesc.DclCount).c_str());
		string += std::format("\t{:64} = {}\n", "TextureNormalInstructions", gToString(inShaderDesc.TextureNormalInstructions).c_str());
		string += std::format("\t{:64} = {}\n", "TextureLoadInstructions", gToString(inShaderDesc.TextureLoadInstructions).c_str());
		string += std::format("\t{:64} = {}\n", "TextureCompInstructions", gToString(inShaderDesc.TextureCompInstructions).c_str());
		string += std::format("\t{:64} = {}\n", "TextureBiasInstructions", gToString(inShaderDesc.TextureBiasInstructions).c_str());
		string += std::format("\t{:64} = {}\n", "TextureGradientInstructions", gToString(inShaderDesc.TextureGradientInstructions).c_str());
		string += std::format("\t{:64} = {}\n", "FloatInstructionCount", gToString(inShaderDesc.FloatInstructionCount).c_str());
		string += std::format("\t{:64} = {}\n", "IntInstructionCount", gToString(inShaderDesc.IntInstructionCount).c_str());
		string += std::format("\t{:64} = {}\n", "UintInstructionCount", gToString(inShaderDesc.UintInstructionCount).c_str());
		string += std::format("\t{:64} = {}\n", "StaticFlowControlCount", gToString(inShaderDesc.StaticFlowControlCount).c_str());
		string += std::format("\t{:64} = {}\n", "DynamicFlowControlCount", gToString(inShaderDesc.DynamicFlowControlCount).c_str());
		string += std::format("\t{:64} = {}\n", "MacroInstructionCount", gToString(inShaderDesc.MacroInstructionCount).c_str());
		string += std::format("\t{:64} = {}\n", "ArrayInstructionCount", gToString(inShaderDesc.ArrayInstructionCount).c_str());
		string += std::format("\t{:64} = {}\n", "CutInstructionCount", gToString(inShaderDesc.CutInstructionCount).c_str());
		string += std::format("\t{:64} = {}\n", "EmitInstructionCount", gToString(inShaderDesc.EmitInstructionCount).c_str());
		string += std::format("\t{:64} = {}\n", "GSOutputTopology", gToString(inShaderDesc.GSOutputTopology).c_str());
		string += std::format("\t{:64} = {}\n", "GSMaxOutputVertexCount", gToString(inShaderDesc.GSMaxOutputVertexCount).c_str());
		string += std::format("\t{:64} = {}\n", "InputPrimitive", gToString(inShaderDesc.InputPrimitive).c_str());
		string += std::format("\t{:64} = {}\n", "PatchConstantParameters", gToString(inShaderDesc.PatchConstantParameters).c_str());
		string += std::format("\t{:64} = {}\n", "cGSInstanceCount", gToString(inShaderDesc.cGSInstanceCount).c_str());
		string += std::format("\t{:64} = {}\n", "cControlPoints", gToString(inShaderDesc.cControlPoints).c_str());
		string += std::format("\t{:64} = {}\n", "HSOutputPrimitive", gToString(inShaderDesc.HSOutputPrimitive).c_str());
		string += std::format("\t{:64} = {}\n", "HSPartitioning", gToString(inShaderDesc.HSPartitioning).c_str());
		string += std::format("\t{:64} = {}\n", "TessellatorDomain", gToString(inShaderDesc.TessellatorDomain).c_str());
		string += std::format("\t{:64} = {}\n", "cBarrierInstructions", gToString(inShaderDesc.cBarrierInstructions).c_str());
		string += std::format("\t{:64} = {}\n", "cInterlockedInstructions", gToString(inShaderDesc.cInterlockedInstructions).c_str());
		string += std::format("\t{:64} = {}\n", "cTextureStoreInstructions", gToString(inShaderDesc.cTextureStoreInstructions).c_str());

		return string;
	}
}

void Renderer::Compiler::Initialize()
{
	// LoadLibraryW + GetProcAddress to eliminate dependency on .lib. Make updating .dll easier.
	mDxcompilerDll = LoadLibraryA("dxcompiler.dll");
	gAssert(mDxcompilerDll != NULL);
	DxcCreateInstanceProc DxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(mDxcompilerDll, "DxcCreateInstance"));

	// See https://simoncoenen.com/blog/programming/graphics/DxcRevised.html
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(mDxcUtils.GetAddressOf()));

	// [NOTE] There is also IDxcCompiler2, IDxcCompiler3. Improves on result handling.
	// https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(mDxcCompiler.GetAddressOf()));

	mDxcUtils->CreateDefaultIncludeHandler(mDxcIncludeHandler.GetAddressOf());

	slang::createGlobalSession(&mGlobalSession);

	CreateCommonRootSignature();
	CreateLocalRootSignature();
}

void Renderer::Compiler::ComputeContributionWeight()
{
	HMODULE dxcompilerDll = mDxcompilerDll;
	*this = {};
	FreeLibrary(dxcompilerDll);
}

void Renderer::Compiler::CreateCommonRootSignature()
{
	D3D12_DESCRIPTOR_RANGE nvapi_range =
	{
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
		.NumDescriptors = 1,
		.BaseShaderRegister = NV_SHADER_EXTN_SLOT,
		.RegisterSpace = NV_SHADER_EXTN_REGISTER_SPACE,
		.OffsetInDescriptorsFromTableStart = 0,
	};

	D3D12_ROOT_PARAMETER root_parameters[] =
	{
		D3D12_ROOT_PARAMETER {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = ROOT_CONSTANTS_REGISTER, .RegisterSpace = COMMON_ROOT_SIGNATURE_REGISTER_SPACE, .Num32BitValues = ROOT_CONSTANTS_NUM_32BIT } },
		D3D12_ROOT_PARAMETER {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = { .ShaderRegister = ROOT_CBV_REGISTER, .RegisterSpace = COMMON_ROOT_SIGNATURE_REGISTER_SPACE }  },
		// https://developer.nvidia.com/blog/improve-shader-performance-and-in-game-frame-rates-with-shader-execution-reordering/
		D3D12_ROOT_PARAMETER {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = D3D12_ROOT_DESCRIPTOR_TABLE { .NumDescriptorRanges = 1, .pDescriptorRanges = &nvapi_range } },
	};

	mCommonRootSignature = CreateRootSignature(
		D3D12_ROOT_SIGNATURE_DESC
		{
			.NumParameters = gArraySize(root_parameters),
			.pParameters = root_parameters,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED,
		});
	gSetName(mCommonRootSignature, "RootSignature.", "Common", "");
}

ComPtr<ID3D12RootSignature> Renderer::Compiler::CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc)
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

ComPtr<ID3D12StateObject> Renderer::Compiler::CreateStateObject(IDxcBlob* inBlob, Shader& ioShader)
{
	// See D3D12_STATE_SUBOBJECT_TYPE
	// https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#d3d12_state_subobject_type
	// Note that all pointers should be valid until CreateStateObject

	std::array<D3D12_STATE_SUBOBJECT, 16> subobjects;
	uint32_t index = 0;

	// DXIL library
	D3D12_DXIL_LIBRARY_DESC dxil_library_desc{ .DXILLibrary = {.pShaderBytecode = inBlob->GetBufferPointer(), .BytecodeLength = inBlob->GetBufferSize() }, .NumExports = 0 /* export everything as long as no name conflict */ };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &dxil_library_desc };

	// Hit group, assume 0 or 1 hit group per lib
	std::wstring hit_group_name = gToWString(ioShader.HitGroupName()); // Need the string object on stack
	std::wstring any_hit_name = gToWString(ioShader.mAnyHitName);
	std::wstring closest_hit_name = gToWString(ioShader.mClosestHitName);
	D3D12_HIT_GROUP_DESC hit_group_desc{ .HitGroupExport = hit_group_name.c_str(), .Type = D3D12_HIT_GROUP_TYPE_TRIANGLES, .AnyHitShaderImport = any_hit_name.c_str(), .ClosestHitShaderImport = closest_hit_name.c_str() };
	if (ioShader.mAnyHitReference != nullptr)
	{
		any_hit_name = gToWString(ioShader.mAnyHitReference->mAnyHitName);
		hit_group_desc.AnyHitShaderImport = any_hit_name.c_str();
	}
	if (!ioShader.mClosestHitName.empty())
		subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group_desc };

	// Local root signature and associations
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, mLocalRootSignature.GetAddressOf() };
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION subobject_to_exports_association{ .pSubobjectToAssociate = &subobjects[index - 1], .NumExports = 0 /* as default association, maybe can be omit? */ };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &subobject_to_exports_association };

	// Shader config
	D3D12_RAYTRACING_SHADER_CONFIG shader_config = { .MaxPayloadSizeInBytes = (uint32_t)sizeof(RayPayload), .MaxAttributeSizeInBytes = sizeof(float) * 2 /* sizeof(BuiltInTriangleIntersectionAttributes) */ };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config };

	// Pipeline config, [0, 31] https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_raytracing_pipeline_config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = { .MaxTraceRecursionDepth = 1 }; // 1 means only TraceRay from raygeneration
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config };

	// Global root signature
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, mCommonRootSignature.GetAddressOf() };

	// State object config
	D3D12_STATE_OBJECT_CONFIG state_object_config = { .Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS };
	if (ioShader.mAnyHitReference != nullptr)
		state_object_config.Flags |= D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS;	// Reference to separated AnyHit
	if (ioShader.mClosestHitName.empty() && !ioShader.mAnyHitName.empty())
		state_object_config.Flags |= D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS;	// Separated AnyHit being referenced
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &state_object_config };

	// Create the state object
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();
	desc.Type = !ioShader.mRayGenerationName.empty() ? D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE : D3D12_STATE_OBJECT_TYPE_COLLECTION;

	ComPtr<ID3D12StateObject> state_object;
	if (FAILED(gDevice->CreateStateObject(&desc, IID_PPV_ARGS(&state_object))))
		return nullptr;

	return state_object;
}

Shader Renderer::Compiler::CombineShader(const Shader& inBaseShader, std::span<Shader> inCollections)
{
	std::vector<D3D12_STATE_SUBOBJECT> subobjects;
	subobjects.resize(1 /* D3D12_STATE_OBJECT_CONFIG */ + inCollections.size());
	uint32_t index = 0;

	D3D12_STATE_OBJECT_CONFIG state_object_config = { .Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &state_object_config };

	std::vector<D3D12_EXISTING_COLLECTION_DESC> collection_descs;
	collection_descs.resize(inCollections.size());
	for (int i = 0; i < inCollections.size(); i++)
	{
		collection_descs[i] = { .pExistingCollection = inCollections[i].mData.mStateObject.Get() };
		subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION, &collection_descs[i] };
	}

	D3D12_STATE_OBJECT_DESC desc = {};
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();

	ComPtr<ID3D12StateObject> state_object;
	gValidate(gDevice->AddToStateObject(&desc, inBaseShader.mData.mStateObject.Get(), IID_PPV_ARGS(state_object.GetAddressOf())));

	Shader output = {};
	output.mData = inBaseShader.mData;
	output.mData.mStateObject = state_object;
	return output;
}

struct ShaderIdentifier
{
	uint8_t							mIdentifier[32] = {};
};

struct ShaderTableEntry
{
	ShaderIdentifier				mShaderIdentifier = {};		// 32 bytes

	// Local Root Parameters, see also gCreateLocalRootSignature
	LocalConstants					mLocalConstants = {};		// 32 bytes, as SetGraphicsRoot32BitConstant for global root signature
};

static_assert(sizeof(ShaderTableEntry::mShaderIdentifier) == D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, "D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES check failed");
static_assert(sizeof(ShaderTableEntry) % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT == 0, "D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT check failed");

void Renderer::Compiler::CreateLocalRootSignature()
{
	// See ShaderTableEntry
	D3D12_ROOT_PARAMETER root_parameters[] =
	{
		D3D12_ROOT_PARAMETER {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .RegisterSpace = LOCAL_ROOT_SIGNATURE_REGISTER_SPACE, .Num32BitValues = 8 } },
	};

	mLocalRootSignature = CreateRootSignature(
		D3D12_ROOT_SIGNATURE_DESC
		{
			.NumParameters = gArraySize(root_parameters),
			.pParameters = root_parameters,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE,
		});
	gSetName(mCommonRootSignature, "RootSignature.", "Local", "");
}

ShaderTable Renderer::Compiler::CreateShaderTable(const Shader& inShader, const Shader& inRayGenerationShader, const Shader& inMissShader)
{
	ShaderTable shader_table;
	if (inShader.mData.mStateObject == nullptr)
		return shader_table;

	// Construct the table
	std::vector<ShaderTableEntry> shader_table_entries;
	{
		// Local root argument layout
		// 
		// Reference
		// - https://github.com/NVIDIAGameWorks/DxrTutorials/blob/dcb8810086f80e77157a6a3b7deff2f24e0986d7/Tutorials/06-Raytrace/06-Raytrace.cpp#L734
		// - https://github.com/NVIDIAGameWorks/Falcor/blob/236927c2bca252f9ea1e3bacb982f8fcba817a67/Framework/Source/Experimental/Raytracing/RtProgramVars.cpp#L116
		// - p21 http://intro-to-dxr.cwyman.org/presentations/IntroDXR_RaytracingAPI.pdf

		// HitGroup table indexing
		// 
		// HitGroupRecordAddress = 
		//		D3D12_DISPATCH_RAYS_DESC.HitGroupTable.StartAddress						// from CPU: DispatchRays()
		//		+				
		//		D3D12_DISPATCH_RAYS_DESC.HitGroupTable.StrideInBytes					// from CPU: DispatchRays()
		//		*
		//		(
		//			RayContributionToHitGroupIndex										// from GPU: TraceRay(). Typically as ray type, e.g. Primary ray, Shadow ray
		//			+
		//			(
		//				MultiplierForGeometryContributionToHitGroupIndex				// from GPU: TraceRay(). Typically as count of ray type, to index for each geometry in BLAS
		//				*
		//				GeometryContributionToHitGroupIndex								// from GPU: Same as GeometryIndex(), index in BLAS
		//			) 
		//			+ 
		//			D3D12_RAYTRACING_INSTANCE_DESC.InstanceContributionToHitGroupIndex	// from CPU: D3D12_RAYTRACING_INSTANCE_DESC. Typically as material index.
		//		)
		// 
		// Reference
		// - Figure 2 https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways
		// - https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#hit-group-table-indexing
		// - https://github.com/NVIDIAGameWorks/Falcor/blob/236927c2bca252f9ea1e3bacb982f8fcba817a67/Framework/Source/Experimental/Raytracing/RtProgramVars.cpp#L131
		// - p24 https://intro-to-dxr.cwyman.org/presentations/IntroDXR_RaytracingAPI.pdf

		ComPtr<ID3D12StateObjectProperties> state_object_properties;
		inShader.mData.mStateObject->QueryInterface(IID_PPV_ARGS(&state_object_properties));

		// RayGen shaders
		{
			shader_table.mRayGenOffset = shader_table_entries.size();

			shader_table_entries.push_back({});
			memcpy(&shader_table_entries.back().mShaderIdentifier, state_object_properties->GetShaderIdentifier(gToWString(inRayGenerationShader.mRayGenerationName).c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entries.back().mLocalConstants.mData0 = { shader_table_entries.size() - 1, 0, 0, 0 };
			shader_table_entries.back().mLocalConstants.mData1 = { 0, 0, 0, 0 };

			shader_table.mRayGenCount = shader_table_entries.size() - shader_table.mRayGenOffset;
		}

		// Miss shaders
		{
			shader_table.mMissOffset = shader_table_entries.size();

			shader_table_entries.push_back({});
			memcpy(&shader_table_entries.back().mShaderIdentifier, state_object_properties->GetShaderIdentifier(gToWString(inMissShader.mMissName).c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entries.back().mLocalConstants.mData0 = { shader_table_entries.size() - 1, 0, 0, 0 };
			shader_table_entries.back().mLocalConstants.mData1 = { 0, 0, 0, 0 };

			shader_table.mMissCount = shader_table_entries.size() - shader_table.mMissOffset;
		}

		// HitGroup shaders
		{
			shader_table.mHitGroupOffset = shader_table_entries.size();

			// Try not to index out of bounds from shader, otherwise GPU may crash...
			for (const Shader& shader : gRenderer.mRuntime.mHitGroupShaders)
			{
				shader_table_entries.push_back({});
				void* shader_identifier = state_object_properties->GetShaderIdentifier(gToWString(shader.HitGroupName()).c_str());
				memcpy(&shader_table_entries.back().mShaderIdentifier, shader_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
				shader_table_entries.back().mLocalConstants.mData0 = { shader_table_entries.size() - 1, 0, 0, 0 };
				shader_table_entries.back().mLocalConstants.mData1 = { 0, 0, 0, 0 };
			}

			shader_table.mHitGroupCount = shader_table_entries.size() - shader_table.mHitGroupOffset;
		}

		shader_table.mEntrySize = sizeof(ShaderTableEntry);
	}

	// Create the table
	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(shader_table.mEntrySize * shader_table_entries.size());

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&shader_table.mResource)));
		shader_table.mResource->SetName(L"ShaderTable");
	}

	// Copy the table
	{
		// Map
		uint8_t* data_pointer;
		gValidate(shader_table.mResource->Map(0, nullptr, (void**)&data_pointer));

		// Copy
		memcpy(data_pointer, shader_table_entries.data(), shader_table.mEntrySize * shader_table_entries.size());

		// Unmap
		shader_table.mResource->Unmap(0, nullptr);
	}

	return shader_table;
}

ComPtr<IDxcBlob> Renderer::Compiler::Compile(const std::string_view& inFilename, const std::string_view& inEntryPoint, const std::string_view& inProfile)
{
	// Generated header
	std::string shader_header;

	// AtmosphereMode
	if (!gAtmosphere.mProfile.mDynamicModeSwitch)
		shader_header += std::format("#define k{} {}::{}\n", nameof::nameof_enum_type<AtmosphereMode>(), nameof::nameof_enum_type<AtmosphereMode>(), nameof::nameof_enum(gAtmosphere.mProfile.mMode));

	// CloudMode
	if (!gCloud.mProfile.mDynamicModeSwitch)
		shader_header += std::format("#define k{} {}::{}\n", nameof::nameof_enum_type<CloudMode>(), nameof::nameof_enum_type<CloudMode>(), nameof::nameof_enum(gCloud.mProfile.mMode));

	// Profile
	shader_header += std::format("#define SHADER_PROFILE_LIB {}\n", inProfile.starts_with("lib") ? 1 : 0);
	shader_header += std::format("#define SHADER_PROFILE_CS {}\n", inProfile.starts_with("cs") ? 1 : 0);
	shader_header += std::format("#define SHADER_PROFILE_PS {}\n", inProfile.starts_with("ps") ? 1 : 0);
	shader_header += std::format("#define SHADER_PROFILE_VS {}\n", inProfile.starts_with("vs") ? 1 : 0);

	// NVAPI
	shader_header += std::format("#define NVAPI_SER {}\n", gNVAPI.mShaderExecutionReorderingSupported ? 1 : 0);
	shader_header += std::format("#define NVAPI_LSS {}\n", gNVAPI.mLinearSweptSpheresSupported ? 1 : 0);
	shader_header += std::format("#define NVAPI_CLUSTERS {}\n", gNVAPI.mClusterSupported && gNVAPI.mClusterEnabled ? 1 : 0);

	// Config
	shader_header += std::format("#define SHADER_DEBUG {}\n", gConfigs.mShaderDebug ? 1 : 0);
	shader_header += std::format("#define USE_TEXTURE {}\n", gConfigs.mUseTexture ? 1 : 0);
	shader_header += std::format("#define NANOVDB_USE_TEXTURE {}\n", gConfigs.mNanoVDBUseTexture ? 1 : 0);

	// BSDF
	std::vector<std::wstring> bsdf_macros;
	bsdf_macros.resize((int)BSDF::Count);
	for (int i = 0; i < (int)BSDF::Count; i++)
	{
		BSDF bsdf = (BSDF)i;
		shader_header += std::format("#define USE_BSDF_{} {}\n", nameof::nameof_enum(bsdf), gConfigs.mSceneBSDFs.find(bsdf) != gConfigs.mSceneBSDFs.end() ? 1 : 0);
	}

	// EntryPoint
	shader_header += std::format("#define ENTRY_POINT_{} {}\n", inEntryPoint, 1);

	std::vector<LPCWSTR> arguments;
	arguments.push_back(L"-WX");									// warning as error
	// arguments.push_back(L"-Wconversion");						// warning on implicit conversion, disabled as 3rd party code is a mess. Implies -Wfloat-conversion, -Wsign-conversion, etc.
																	// there is no option to only warn about "implicit truncation of vector type" 
	arguments.push_back(L"-all_resources_bound");					// assume all resources bound
	arguments.push_back(L"-Zi");									// .pdb
	arguments.push_back(L"-Qembed_debug");							// embeded .pdb
	arguments.push_back(L"-HV 2021");								// more like c++
	arguments.push_back(L"-disable-payload-qualifiers");			// -disable-payload-qualifiers, see https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#payload-access-qualifiers
	arguments.push_back(L"-enable-16bit-types");					// half

	// Load shader
	std::ifstream shader_file(inFilename.data());
	std::stringstream shader_stream;
	shader_stream << shader_file.rdbuf();
	std::string shader_string = shader_header + "\n" + shader_stream.str();

	ComPtr<IDxcBlob> blob_output;
	if (gToLower(std::filesystem::path(inFilename).extension().string()) == ".slang")
	{
		// An almost trivial .slang takes 3s to compile in debug build... Only trigger compile when enabled
		if (!gConfigs.mTestSlangShader) { return nullptr; }

		using namespace slang;

		auto trace_blob = [](ComPtr<IBlob>& blob)
		{
			if (blob == nullptr) { return; }
			std::string_view blob_string((const char*)blob->getBufferPointer(), blob->getBufferSize());
			gTrace(blob_string);
		};

		std::string downstreamArgs;
		for (auto&& argument : arguments)
			downstreamArgs += gToUTF8String(argument) + "\n";
		downstreamArgs.pop_back(); // pop last '\n'
		std::replace(downstreamArgs.begin(), downstreamArgs.end(), ' ', '\n');
		CompilerOptionEntry options_target[] =
		{
			// Backend options, see also DXCDownstreamCompiler::compile
			{
				// Pass down same arguments used to compile HLSL
				.name = CompilerOptionName::DownstreamArgs,
				.value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "dxc", .stringValue1 = downstreamArgs.data() },
			},
			{
				// Mapped to /Zi, not sure how it affect Slang -> HLSL
				.name = CompilerOptionName::DebugInformation,
				.value = {.kind = CompilerOptionValueKind::Int, .intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL },
			},
			{
				// Mapped to /O3, same as DXC default, not sure how it affect Slang -> HLSL
				.name = CompilerOptionName::Optimization,
				.value = {.kind = CompilerOptionValueKind::Int, .intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL },
			},
		};

		TargetDesc target = {};
		target.format = SLANG_DXIL;
		target.profile = mGlobalSession->findProfile(inProfile.data());
		target.compilerOptionEntries = options_target;
		target.compilerOptionEntryCount = gArraySize(options_target);

		CompilerOptionEntry options_session[] =
		{
			// Frontend options
			{
				// Disable warning on using int macro as bool, "implicit conversion from 'int' to 'bool' is not recommended"
				// However Slang pass "-no-warnings" to DXC, it should be better to keep warnings enabled for Slang
				.name = CompilerOptionName::DisableWarnings,
				.value = {.kind = CompilerOptionValueKind::String, .stringValue0 = "30081" },
			},
		};
		SessionDesc sessionDesc = {};
		sessionDesc.targets = &target;
		sessionDesc.targetCount = 1;
		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
		sessionDesc.compilerOptionEntries = options_session;
		sessionDesc.compilerOptionEntryCount = gArraySize(options_session);

		ComPtr<ISession> session;
		mGlobalSession->createSession(sessionDesc, &session);

		ComPtr<IBlob> diagnostics;
		ComPtr<IModule> module(session->loadModuleFromSourceString(inFilename.data(), inFilename.data(), shader_string.data(), &diagnostics));
		trace_blob(diagnostics);
		if (module == nullptr) { return nullptr; }

		ComPtr<IEntryPoint> entryPoint;
		gValidate(module->findEntryPointByName(inEntryPoint.data(), &entryPoint));

		IComponentType* components[] = { module.Get(), entryPoint.Get() };
		ComPtr<IComponentType> program;
		gValidate(session->createCompositeComponentType(components, 2, &program, &diagnostics));
		trace_blob(diagnostics);

		ComPtr<IComponentType> linkedProgram;
		gValidate(program->link(&linkedProgram, &diagnostics));
		trace_blob(diagnostics);

		ComPtr<IBlob> dxilBlob;
		gValidate(linkedProgram->getEntryPointCode(0, 0, &dxilBlob, &diagnostics));
		trace_blob(diagnostics);
		gValidate(dxilBlob.As(&blob_output));
	}
	else
	{
#pragma warning(disable: 6387) // Warning on pass nullptr to DXC API
		IDxcBlobEncoding* blob_encoding = nullptr;
		gValidate(mDxcUtils->CreateBlobFromPinned(shader_string.c_str(), static_cast<uint32_t>(shader_string.length()), CP_UTF8, &blob_encoding));
		IDxcOperationResult* operation_result = nullptr;
		gValidate(mDxcCompiler->Compile(
			blob_encoding,												// program text
			gToWString(inFilename).c_str(),								// file name, mostly for error messages
			gToWString(inEntryPoint).c_str(),							// entry point function
			gToWString(inProfile).c_str(),								// target profile
			arguments.data(), static_cast<UINT32>(arguments.size()),	// compilation arguments and their count
			nullptr, 0,													// name/value defines and their count
			mDxcIncludeHandler.Get(),									// handler for #include directives
			&operation_result));

		HRESULT compile_result;
		gValidate(operation_result->GetStatus(&compile_result));
		if (FAILED(compile_result))
		{
			IDxcBlobEncoding* blob_error = nullptr;
			IDxcBlobUtf8* blob_error_utf8 = nullptr;
			gValidate(operation_result->GetErrorBuffer(&blob_error));
			// We can use the library to get our preferred encoding.
			gValidate(mDxcUtils->GetBlobAsUtf8(blob_error, &blob_error_utf8));
			std::string str((char*)blob_error_utf8->GetBufferPointer(), blob_error_utf8->GetBufferSize() - 1);
			gTrace(str.c_str());
			blob_error->Release();
			blob_error_utf8->Release();
			return nullptr;
		}
		gValidate(operation_result->GetResult(&blob_output));

		if (std::string_view("RayQueryCS") == inEntryPoint)
		{
			DxcBuffer dxc_buffer{ .Ptr = blob_output->GetBufferPointer(), .Size = blob_output->GetBufferSize(), .Encoding = DXC_CP_ACP };
			ComPtr<ID3D12ShaderReflection> shader_reflection;
			mDxcUtils->CreateReflection(&dxc_buffer, IID_PPV_ARGS(&shader_reflection));

			using RendererHelper::D3D_SHADER_REQUIRES;
			D3D_SHADER_REQUIRES shader_requires = (D3D_SHADER_REQUIRES)shader_reflection->GetRequiresFlags();
			gAssert(((uint)shader_requires & (uint)D3D_SHADER_REQUIRES::REQUIRES_DOUBLES) == 0);

			D3D12_SHADER_DESC shader_desc;
			shader_reflection->GetDesc(&shader_desc);
			gStats.mInstructionCount.mRayQuery = shader_desc.InstructionCount;

			if (gRenderer.mDumpRayQuery)
			{
				IDxcBlobEncoding* blob_disassembled = nullptr;
				ComPtr<IDxcBlobUtf8> blob_disassembled_utf8 = nullptr;
				mDxcCompiler->Disassemble(blob_output.Get(), &blob_disassembled);
				gValidate(mDxcUtils->GetBlobAsUtf8(blob_disassembled, &blob_disassembled_utf8));
				std::string_view shader_disassembled((char*)blob_disassembled_utf8->GetBufferPointer(), blob_disassembled_utf8->GetBufferSize());

				std::filesystem::path path = gEnsureDumpDirectoryExists();
				path += "RayQueryCS.txt";
				std::ofstream stream(path);
				stream << shader_string;
				stream << "\n";
				stream << shader_disassembled;
				stream << "\n";
				stream << RendererHelper::ShaderDescToString(shader_desc);
				stream.close();

				gRenderer.mDumpRayQuery = false;
			}
		}
#pragma warning(default: 6387)
	}

	return blob_output;
}

bool Renderer::Compiler::CreateVSPSPipelineState(const std::string_view& inFileName, const std::string_view& inVSName, const std::string_view& inPSName, Shader& ioShader)
{
	CPUTimingScope timing_scope;
	timing_scope.mTraceName = std::format("CreateVSPSPipelineState [{}], [{}]", inVSName, inPSName);

	ComPtr<IDxcBlob> vs_blob = Compile(inFileName, inVSName, "vs_6_9");
	ComPtr<IDxcBlob> ps_blob = Compile(inFileName, inPSName, "ps_6_9");
	if (vs_blob == nullptr || ps_blob == nullptr)
		return false;

	D3D12_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizer_desc.AntialiasedLineEnable = false; // Tried, but "Alpha antialiased" does not looks clean on line, see https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_rasterizer_desc
	rasterizer_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF; // Only triangles support it

	D3D12_BLEND_DESC blend_desc = {};
	blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.VS.pShaderBytecode = vs_blob->GetBufferPointer();
	pipeline_state_desc.VS.BytecodeLength = vs_blob->GetBufferSize();
	pipeline_state_desc.PS.pShaderBytecode = ps_blob->GetBufferPointer();
	pipeline_state_desc.PS.BytecodeLength = ps_blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = mCommonRootSignature.Get();
	pipeline_state_desc.RasterizerState = rasterizer_desc;
	pipeline_state_desc.BlendState = blend_desc;
	pipeline_state_desc.DepthStencilState.DepthEnable = TRUE;
	pipeline_state_desc.DepthStencilState.DepthWriteMask = ioShader.mDepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	pipeline_state_desc.DepthStencilState.DepthFunc = ioShader.mDepthFunc;
	pipeline_state_desc.SampleMask = UINT_MAX;
	pipeline_state_desc.PrimitiveTopologyType = ioShader.mTopology;
	pipeline_state_desc.NumRenderTargets = ioShader.mRTVFormat != DXGI_FORMAT_UNKNOWN ? 1 : 0;
	pipeline_state_desc.RTVFormats[0] = ioShader.mRTVFormat;
	pipeline_state_desc.DSVFormat = ioShader.mDSVFormat;
	pipeline_state_desc.SampleDesc.Count = 1;

	if (FAILED(gDevice->CreateGraphicsPipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioShader.mData.mPipelineState))))
		return false;

	gSetName(ioShader.mData.mPipelineState, "PipelineState.", std::format("{}_{}", inVSName, inPSName), "");

	return true;
}

bool Renderer::Compiler::CreateCSPipelineState(const std::string_view& inFileName, const std::string_view& inCSName, Shader& ioShader)
{
	CPUTimingScope timing_scope;
	timing_scope.mTraceName = std::format("CreateCSPipelineState   [{}]", inCSName);

	ComPtr<IDxcBlob> blob = Compile(inFileName, inCSName, "cs_6_9");
	if (blob == nullptr)
		return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.CS.pShaderBytecode = blob->GetBufferPointer();
	pipeline_state_desc.CS.BytecodeLength = blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = mCommonRootSignature.Get();
	if (FAILED(gDevice->CreateComputePipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioShader.mData.mPipelineState))))
		return false;

	gSetName(ioShader.mData.mPipelineState, "PipelineState.", inCSName.data(), "");

	return true;
}

bool Renderer::Compiler::CreateLibPipelineState(const std::string_view& inFileName, const std::string_view& inLibName, Shader& ioShader)
{
	CPUTimingScope timing_scope;
	timing_scope.mTraceName = std::format("CreateLibPipelineState  [{}]", inLibName);

	ComPtr<IDxcBlob> blob = Compile(inFileName, "", "lib_6_9");
	if (blob == nullptr)
		return false;

	ioShader.mData.mStateObject = CreateStateObject(blob.Get(), ioShader);
	if (ioShader.mData.mStateObject == nullptr)
		return false;

	gSetName(ioShader.mData.mStateObject, "StateObject.", inLibName.data(), "");

	return true;
}

bool Renderer::Compiler::CompileShader(Shader& ioShader)
{
	if (!ioShader.mRayGenerationName.empty())
		return CreateLibPipelineState(ioShader.mFileName, ioShader.mRayGenerationName, ioShader);
	else if (!ioShader.mMissName.empty())
		return CreateLibPipelineState(ioShader.mFileName, ioShader.mMissName, ioShader);
	else if (!ioShader.HitName().empty())
		return CreateLibPipelineState(ioShader.mFileName, ioShader.HitName(), ioShader);
	else if (!ioShader.mCSName.empty())
		return CreateCSPipelineState(ioShader.mFileName, ioShader.mCSName, ioShader);
	else
		return CreateVSPSPipelineState(ioShader.mFileName, ioShader.mVSName, ioShader.mPSName, ioShader);
}

void Renderer::Initialize()
{
	mCompiler.Initialize();

	for (auto&& texture : mRuntime.mTextures)
		texture.Initialize();
	
	for (auto&& buffer : mRuntime.mBuffers)
		buffer.Initialize();

	InitializeShaders();
	InitializeScreenSizeTextures();
}

void Renderer::ComputeContributionWeight()
{
	mRuntime.Reset();
	mCompiler.ComputeContributionWeight();
}

void Renderer::Render(ID3D12GraphicsCommandList4* inCommandList)
{
	for (auto&& texture : mRuntime.mTextures)
		texture.UpdateGPU(inCommandList);

	if (mSpatialCacheResetRequested)
	{
		uint4 clear_value_uint = { 0, 0, 0, 0 };

		gRenderer.Setup(gRenderer.mRuntime.mClearBufferShader, { .mData0 = { mRuntime.mSpatialHashBuffer.mUAVIndex, ClearMode::UInt4, 0, 0 }, .mData1 = clear_value_uint });
		inCommandList->Dispatch(gAlignUpDiv(mRuntime.mSpatialHashBuffer.GetSizeInBytes() / 16 /* UInt4 */, 64u), 1, 1);

		gRenderer.Setup(gRenderer.mRuntime.mClearBufferShader, { .mData0 = { mRuntime.mSpatialDataBuffer.mUAVIndex, ClearMode::UInt4, 0, 0 }, .mData1 = clear_value_uint });
		inCommandList->Dispatch(gAlignUpDiv(mRuntime.mSpatialHashBuffer.GetSizeInBytes() / 16 /* UInt4 */, 64u), 1, 1);

		mSpatialCacheResetRequested = false;
	}
}

void Renderer::ImGuiShowTextures()
{
	ImGui::Textures(mRuntime.mScreenTextures, "Renderer.Screen", ImGuiTreeNodeFlags_DefaultOpen);
	ImGui::Textures(mRuntime.mTextures, "Renderer", ImGuiTreeNodeFlags_DefaultOpen);
}

void Renderer::InitializeScreenSizeTextures()
{
	if (!gHeadless)
	{
		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
		gSwapChain->GetDesc1(&swap_chain_desc);

		for (int i = 0; i < kFrameInFlightCount; i++)
		{
			gSwapChain->GetBuffer(i, IID_PPV_ARGS(mRuntime.mBackBuffers[i].mResource.GetAddressOf()));
			std::wstring name = gToWString(mRuntime.mBackBuffers[i].mName + gToString(i));
			mRuntime.mBackBuffers[i].mResource->SetName(name.c_str());

			mRuntime.mBackBuffers[i].mWidth = swap_chain_desc.Width;
			mRuntime.mBackBuffers[i].mHeight = swap_chain_desc.Height;
			mRuntime.mBackBuffers[i].mFormat = swap_chain_desc.Format;

			RTVDescriptorIndex index = RTVDescriptorIndex((uint)mRuntime.mBackBuffers[i].mRTVIndex);
			D3D12_CPU_DESCRIPTOR_HANDLE handle = gCPUContext.mRTVDescriptorHeap.GetCPUHandle(index);
			gDevice->CreateRenderTargetView(mRuntime.mBackBuffers[i].mResource.Get(), nullptr, handle);
		}

		mScreenSize.x = swap_chain_desc.Width;
		mScreenSize.y = swap_chain_desc.Height;
	}

	for (auto&& screen_texture : mRuntime.mScreenTextures)
		screen_texture.Width(mScreenSize.x).Height(mScreenSize.y).Initialize();
}

void Renderer::FinalizeScreenSizeTextures()
{
	for (int i = 0; i < kFrameInFlightCount; i++)
		mRuntime.mBackBuffers[i].mResource = nullptr;
}

void Renderer::InitializeShaders()
{
	for (auto&& shader : mRuntime.mShaders)
		mCompiler.CompileShader(shader);

	if (gConfigs.mTestHitShader)
	{
		mCompiler.CompileShader(mRuntime.mRayGenerationShader);
		for (auto&& shader : mRuntime.mCollectionShaders)
			mCompiler.CompileShader(shader);
		mRuntime.mLibShader = mCompiler.CombineShader(mRuntime.mRayGenerationShader, mRuntime.mCollectionShaders);
		mRuntime.mLibShaderTable = mCompiler.CreateShaderTable(mRuntime.mLibShader, mRuntime.mRayGenerationShader, mRuntime.mMissShader);
	}

	if (gAtmosphere.mEnabled)
	{
		for (auto&& shaders : gAtmosphere.mRuntime.mShadersSet)
			for (auto&& shader : shaders)
				mCompiler.CompileShader(shader);
	}

	if (gCloud.mEnabled)
	{
		for (auto&& shader : gCloud.mRuntime.mShaders)
			mCompiler.CompileShader(shader);
	}
}

void Renderer::FinalizeShaders()
{
	// No actual cleanup in case rebuild fails
}

Renderer gRenderer;
GPUTiming gGPUTiming;
