
HMODULE Dxcompiler_dll = NULL;
ComPtr<IDxcUtils> DxcUtils;
ComPtr<IDxcCompiler> DxcCompiler;
ComPtr<IDxcIncludeHandler> DxcIncludeHandler;
void gInitializeDxcInterfaces()
{
	// LoadLibraryW + GetProcAddress to eliminate dependency on .lib. Make updating .dll easier.
	Dxcompiler_dll = LoadLibraryW(L"dxcompiler.dll");
	gAssert(Dxcompiler_dll != NULL);
	DxcCreateInstanceProc DxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(Dxcompiler_dll, "DxcCreateInstance"));

	// See https://simoncoenen.com/blog/programming/graphics/DxcRevised.html
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(DxcUtils.GetAddressOf()));

	// [NOTE] There is also IDxcCompiler2, IDxcCompiler3. Improves on result handling.
	// https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(DxcCompiler.GetAddressOf()));

	DxcUtils->CreateDefaultIncludeHandler(DxcIncludeHandler.GetAddressOf());
}

void gFinalizeDxcInterfaces()
{
	DxcUtils = nullptr;
	DxcCompiler = nullptr;
	DxcIncludeHandler = nullptr;

	FreeLibrary(Dxcompiler_dll);
}

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

void gInspectShaderReflection(ComPtr<IDxcBlob> inBlob, Shader& ioShader)
{
	// For debug only

	UNUSED(ioShader);

	DxcBuffer dxc_buffer{ .Ptr = inBlob->GetBufferPointer(), .Size = inBlob->GetBufferSize(), .Encoding = DXC_CP_ACP };
	ComPtr<ID3D12ShaderReflection> shader_reflection;
	DxcUtils->CreateReflection(&dxc_buffer, IID_PPV_ARGS(&shader_reflection));

	D3D12_SHADER_DESC shader_desc;
	shader_reflection->GetDesc(&shader_desc);
	UNUSED(shader_desc);

	enum class D3D_SHADER_REQUIRES
	{
		REQUIRES_DOUBLES                                                         = D3D_SHADER_REQUIRES_DOUBLES,
		REQUIRES_EARLY_DEPTH_STENCIL                                             = D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL,
		REQUIRES_UAVS_AT_EVERY_STAGE                                             = D3D_SHADER_REQUIRES_UAVS_AT_EVERY_STAGE,
		REQUIRES_64_UAVS                                                         = D3D_SHADER_REQUIRES_64_UAVS,
		REQUIRES_MINIMUM_PRECISION                                               = D3D_SHADER_REQUIRES_MINIMUM_PRECISION,
		REQUIRES_11_1_DOUBLE_EXTENSIONS                                          = D3D_SHADER_REQUIRES_11_1_DOUBLE_EXTENSIONS,
		REQUIRES_11_1_SHADER_EXTENSIONS                                          = D3D_SHADER_REQUIRES_11_1_SHADER_EXTENSIONS,
		REQUIRES_LEVEL_9_COMPARISON_FILTERING                                    = D3D_SHADER_REQUIRES_LEVEL_9_COMPARISON_FILTERING,
		REQUIRES_TILED_RESOURCES                                                 = D3D_SHADER_REQUIRES_TILED_RESOURCES,
		REQUIRES_STENCIL_REF                                                     = D3D_SHADER_REQUIRES_STENCIL_REF,
		REQUIRES_INNER_COVERAGE                                                  = D3D_SHADER_REQUIRES_INNER_COVERAGE,
		REQUIRES_TYPED_UAV_LOAD_ADDITIONAL_FORMATS                               = D3D_SHADER_REQUIRES_TYPED_UAV_LOAD_ADDITIONAL_FORMATS,
		REQUIRES_ROVS                                                            = D3D_SHADER_REQUIRES_ROVS,
		REQUIRES_VIEWPORT_AND_RT_ARRAY_INDEX_FROM_ANY_SHADER_FEEDING_RASTERIZER  = D3D_SHADER_REQUIRES_VIEWPORT_AND_RT_ARRAY_INDEX_FROM_ANY_SHADER_FEEDING_RASTERIZER,
		REQUIRES_WAVE_OPS                                                        = D3D_SHADER_REQUIRES_WAVE_OPS,
		REQUIRES_INT64_OPS                                                       = D3D_SHADER_REQUIRES_INT64_OPS,
		REQUIRES_VIEW_ID                                                         = D3D_SHADER_REQUIRES_VIEW_ID,
		REQUIRES_BARYCENTRICS                                                    = D3D_SHADER_REQUIRES_BARYCENTRICS,
		REQUIRES_NATIVE_16BIT_OPS                                                = D3D_SHADER_REQUIRES_NATIVE_16BIT_OPS,
		REQUIRES_SHADING_RATE                                                    = D3D_SHADER_REQUIRES_SHADING_RATE,
		REQUIRES_RAYTRACING_TIER_1_1                                             = D3D_SHADER_REQUIRES_RAYTRACING_TIER_1_1,
		REQUIRES_SAMPLER_FEEDBACK                                                = D3D_SHADER_REQUIRES_SAMPLER_FEEDBACK,
		REQUIRES_ATOMIC_INT64_ON_TYPED_RESOURCE                                  = D3D_SHADER_REQUIRES_ATOMIC_INT64_ON_TYPED_RESOURCE,
		REQUIRES_ATOMIC_INT64_ON_GROUP_SHARED                                    = D3D_SHADER_REQUIRES_ATOMIC_INT64_ON_GROUP_SHARED,
		REQUIRES_DERIVATIVES_IN_MESH_AND_AMPLIFICATION_SHADERS                   = D3D_SHADER_REQUIRES_DERIVATIVES_IN_MESH_AND_AMPLIFICATION_SHADERS,
		REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING                               = D3D_SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING,
		REQUIRES_SAMPLER_DESCRIPTOR_HEAP_INDEXING                                = D3D_SHADER_REQUIRES_SAMPLER_DESCRIPTOR_HEAP_INDEXING,
		REQUIRES_WAVE_MMA                                                        = D3D_SHADER_REQUIRES_WAVE_MMA,
		REQUIRES_ATOMIC_INT64_ON_DESCRIPTOR_HEAP_RESOURCE                        = D3D_SHADER_REQUIRES_ATOMIC_INT64_ON_DESCRIPTOR_HEAP_RESOURCE,
		FEATURE_ADVANCED_TEXTURE_OPS                                             = D3D_SHADER_FEATURE_ADVANCED_TEXTURE_OPS,
		FEATURE_WRITEABLE_MSAA_TEXTURES                                          = D3D_SHADER_FEATURE_WRITEABLE_MSAA_TEXTURES,
	};

	D3D_SHADER_REQUIRES required_flags = (D3D_SHADER_REQUIRES)shader_reflection->GetRequiresFlags();
	UNUSED(required_flags);
}

void gInspectLibraryReflection(ComPtr<IDxcBlob> inBlob, Shader& ioShader)
{
	// For debug only
	
	UNUSED(ioShader);

	// Note function name here is mangled, and other information does not seem reliable...

	DxcBuffer dxc_buffer{ .Ptr = inBlob->GetBufferPointer(), .Size = inBlob->GetBufferSize(), .Encoding = DXC_CP_ACP };
	ComPtr<ID3D12LibraryReflection> library_reflection;
	DxcUtils->CreateReflection(&dxc_buffer, IID_PPV_ARGS(&library_reflection));

	D3D12_LIBRARY_DESC library_desc;
	library_reflection->GetDesc(&library_desc);
	UNUSED(library_desc);

	ID3D12FunctionReflection* function_reflection = library_reflection->GetFunctionByIndex(0);
	D3D12_FUNCTION_DESC function_desc;
	function_reflection->GetDesc(&function_desc);
	UNUSED(function_desc);
}

ComPtr<ID3D12StateObject> gCreateStateObject(IDxcBlob* inBlob, Shader& ioShader)
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
	std::wstring hit_group_name = ioShader.HitGroupName(); // Need the string object on stack
	D3D12_HIT_GROUP_DESC hit_group_desc{ .HitGroupExport = hit_group_name.c_str(), .Type = D3D12_HIT_GROUP_TYPE_TRIANGLES, .AnyHitShaderImport = ioShader.mAnyHitName, .ClosestHitShaderImport = ioShader.mClosestHitName };
	if (ioShader.mAnyHitReference != nullptr)
		hit_group_desc.AnyHitShaderImport = ioShader.mAnyHitReference->mAnyHitName;
	if (ioShader.mClosestHitName != nullptr)
		subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group_desc };

	// Local root signature and associations
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, gRenderer.mRuntime.mLibLocalRootSignature.GetAddressOf() };
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION subobject_to_exports_association{ .pSubobjectToAssociate = &subobjects[index - 1], .NumExports = 0 /* as default association, maybe can be omit? */ };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &subobject_to_exports_association };

	// Shader config
	D3D12_RAYTRACING_SHADER_CONFIG shader_config = { .MaxPayloadSizeInBytes = (uint32_t)sizeof(RayPayload), .MaxAttributeSizeInBytes = sizeof(float) * 2 /* sizeof(BuiltInTriangleIntersectionAttributes) */ };
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config };

	// Pipeline config, [0, 31] https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_raytracing_pipeline_config
	D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = { .MaxTraceRecursionDepth = 1 }; // 1 means only TraceRay from raygeneration
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config };

	// Global root signature
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, ioShader.mRootSignatureReference->mData.mRootSignature.GetAddressOf() };

	// State object config
	D3D12_STATE_OBJECT_CONFIG state_object_config = { .Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS };
	if (ioShader.mAnyHitReference != nullptr)
		state_object_config.Flags |= D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS;	// Reference to separated AnyHit
	if (ioShader.mClosestHitName == nullptr && ioShader.mAnyHitName != nullptr)
		state_object_config.Flags |= D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS;	// Separated AnyHit being referenced
	subobjects[index++] = D3D12_STATE_SUBOBJECT{ D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, &state_object_config };

	// Create the state object
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();
	desc.Type = ioShader.mRayGenerationName != nullptr ? D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE : D3D12_STATE_OBJECT_TYPE_COLLECTION;

	ComPtr<ID3D12StateObject> state_object;
	if (FAILED(gDevice->CreateStateObject(&desc, IID_PPV_ARGS(&state_object))))
		return nullptr;

	return state_object;
}

void gCombineShader(const Shader& inBaseShader, std::span<Shader> inCollections, Shader& outShader)
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

	outShader.mData = inBaseShader.mData;
	outShader.mData.mStateObject = state_object;
}

struct ShaderIdentifier
{
	uint8_t							mIdentifier[32] = {};
};

struct ShaderTableEntry
{
	ShaderIdentifier				mShaderIdentifier = {};		// 32 bytes

	// Local Root Parameters, see also gCreateLocalRootSignature
	LocalConstants					mLocalConstants = {};		// 16 bytes, as SetGraphicsRoot32BitConstant for global root signature
	D3D12_GPU_VIRTUAL_ADDRESS		mLocalCBV = {};				// 8 bytes, as SetGraphicsRootConstantBufferView for global root signature
	D3D12_GPU_DESCRIPTOR_HANDLE		mLocalSRVs = {};			// 8 bytes, as SetGraphicsRootDescriptorTable for global root signature
};

static_assert(sizeof(ShaderTableEntry::mShaderIdentifier) == D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, "D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES check failed");
static_assert(sizeof(ShaderTableEntry) % D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT == 0, "D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT check failed");

ComPtr<ID3D12RootSignature> gCreateLocalRootSignature()
{
	uint32_t local_root_signature_register_space = 100; // As long as no overlap with global root signature

	D3D12_DESCRIPTOR_RANGE local_root_descriptor_table_range =
	{
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		.NumDescriptors = 4096, // Unbounded, match size with referencing FrameContext::mViewDescriptorHeap
		.BaseShaderRegister = 0,
		.RegisterSpace = local_root_signature_register_space,
		.OffsetInDescriptorsFromTableStart = 0,
	};

	// Local Root Parameters, see also ShaderTableEntry
	D3D12_ROOT_PARAMETER local_root_parameters[] =
	{
		D3D12_ROOT_PARAMETER {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .RegisterSpace = local_root_signature_register_space, .Num32BitValues = 4 } },
		D3D12_ROOT_PARAMETER {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = {.ShaderRegister = 1, .RegisterSpace = local_root_signature_register_space }  },
		D3D12_ROOT_PARAMETER {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = D3D12_ROOT_DESCRIPTOR_TABLE {.NumDescriptorRanges = 1, .pDescriptorRanges = &local_root_descriptor_table_range } },
	};

	ComPtr<ID3D12RootSignature> local_root_signature = gCreateRootSignature(
		D3D12_ROOT_SIGNATURE_DESC
		{
			.NumParameters = ARRAYSIZE(local_root_parameters),
			.pParameters = local_root_parameters,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE,
		});
	local_root_signature->SetName(L"LocalRootSignature");

	return local_root_signature;
}

ShaderTable gCreateShaderTable(const Shader& inShader)
{
	ShaderTable shader_table;
	if (inShader.mData.mStateObject == nullptr)
		return shader_table;

	// Construct the table
	std::vector<ShaderTableEntry> shader_table_entries;
	{
		// Local root argument layout
		// 
		// 
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
			memcpy(&shader_table_entries.back().mShaderIdentifier, state_object_properties->GetShaderIdentifier(gRenderer.mRuntime.mRayGenerationShader.mRayGenerationName), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entries.back().mLocalConstants.mShaderIndex = static_cast<uint32_t>(shader_table_entries.size() - 1);
			shader_table_entries.back().mLocalCBV = gConstantBuffer->GetGPUVirtualAddress();
			shader_table_entries.back().mLocalSRVs = gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::Invalid);

			shader_table.mRayGenCount = shader_table_entries.size() - shader_table.mRayGenOffset;
		}

		// Miss shaders
		{
			shader_table.mMissOffset = shader_table_entries.size();

			shader_table_entries.push_back({});
			memcpy(&shader_table_entries.back().mShaderIdentifier, state_object_properties->GetShaderIdentifier(gRenderer.mRuntime.mMissShader.mMissName), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entries.back().mLocalConstants.mShaderIndex = static_cast<uint32_t>(shader_table_entries.size() - 1);
			shader_table_entries.back().mLocalCBV = gConstantBuffer->GetGPUVirtualAddress();
			shader_table_entries.back().mLocalSRVs = gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::Invalid);

			shader_table.mMissCount = shader_table_entries.size() - shader_table.mMissOffset;
		}

		// HitGroup shaders
		{
			shader_table.mHitGroupOffset = shader_table_entries.size();

			// Try not to index out of bounds from shader, otherwise GPU may crash...
			for (const Shader& shader : gRenderer.mRuntime.mHitGroupShaders)
			{
				shader_table_entries.push_back({});
				memcpy(&shader_table_entries.back().mShaderIdentifier, state_object_properties->GetShaderIdentifier(shader.HitGroupName().c_str()), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
				shader_table_entries.back().mLocalConstants.mShaderIndex = static_cast<uint32_t>(shader_table_entries.size() - 1);
				shader_table_entries.back().mLocalCBV = gConstantBuffer->GetGPUVirtualAddress();
				shader_table_entries.back().mLocalSRVs = gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::Invalid);
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


ComPtr<IDxcBlob> gCompileShader(const char* inFilename, const char* inEntryPoint, const char* inProfile)
{
	// Load shader
	std::ifstream shader_file(inFilename);
	std::stringstream shader_stream;
	shader_stream << shader_file.rdbuf();
	std::string shader_string = shader_stream.str();

	IDxcBlobEncoding* blob_encoding;
	gValidate(DxcUtils->CreateBlobFromPinned(shader_string.c_str(), static_cast<uint32_t>(shader_string.length()), CP_UTF8, &blob_encoding));

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
	if (FAILED(DxcCompiler->Compile(
		blob_encoding,												// program text
		wfilename.c_str(),											// file name, mostly for error messages
		entry_point.c_str(),										// entry point function
		profile.c_str(),											// target profile
		arguments.data(), static_cast<UINT32>(arguments.size()),	// compilation arguments and their count
		defines.data(), static_cast<UINT32>(defines.size()),		// name/value defines and their count
		DxcIncludeHandler.Get(),										// handler for #include directives
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
		gValidate(DxcUtils->GetBlobAsUtf8(blob, &blob_8));
		std::string str((char*)blob_8->GetBufferPointer(), blob_8->GetBufferSize() - 1);
		gTrace(str.c_str());
		blob->Release();
		blob_8->Release();
		return nullptr;
	}

	ComPtr<IDxcBlob> blob = nullptr;
	gValidate(operation_result->GetResult(&blob));

	if (gRenderer.mDumpDisassemblyRayQuery && std::string_view("RayQueryCS") == inEntryPoint)
	{
		ComPtr<IDxcBlob> blob_to_dissemble = blob;
		IDxcBlobEncoding* disassemble = nullptr;
		ComPtr<IDxcBlobUtf8> blob_8 = nullptr;
		DxcCompiler->Disassemble(blob_to_dissemble.Get(), &disassemble);
		gValidate(DxcUtils->GetBlobAsUtf8(disassemble, &blob_8));
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

bool gCreateVSPSPipelineState(const char* inShaderFileName, const char* inVSName, const char* inPSName, Shader& ioShader)
{
	ComPtr<IDxcBlob> vs_blob = gCompileShader(inShaderFileName, inVSName, "vs_6_6");
	ComPtr<IDxcBlob> ps_blob = gCompileShader(inShaderFileName, inPSName, "ps_6_6");
	if (vs_blob == nullptr || ps_blob == nullptr)
		return false;

	if (FAILED(gDevice->CreateRootSignature(0, ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), IID_PPV_ARGS(&ioShader.mData.mRootSignature))))
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
	pipeline_state_desc.pRootSignature = ioShader.mData.mRootSignature.Get();
	pipeline_state_desc.RasterizerState = rasterizer_desc;
	pipeline_state_desc.BlendState = blend_desc;
	pipeline_state_desc.DepthStencilState.DepthEnable = FALSE;
	pipeline_state_desc.DepthStencilState.StencilEnable = FALSE;
	pipeline_state_desc.SampleMask = UINT_MAX;
	pipeline_state_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipeline_state_desc.NumRenderTargets = 1;
	pipeline_state_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	pipeline_state_desc.SampleDesc.Count = 1;

	if (FAILED(gDevice->CreateGraphicsPipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioShader.mData.mPipelineState))))
		return false;

	std::wstring name = gToWString(inVSName) + L"_" + gToWString(inPSName);
	ioShader.mData.mPipelineState->SetName(name.c_str());

	gInspectShaderReflection(vs_blob, ioShader);
	gInspectShaderReflection(ps_blob, ioShader);

	return true;
}

bool gCreateCSPipelineState(const char* inShaderFileName, const char* inCSName, Shader& ioShader)
{
	ComPtr<IDxcBlob> blob = gCompileShader(inShaderFileName, inCSName, "cs_6_6");
	if (blob == nullptr)
		return false;

	LPVOID root_signature_pointer = blob->GetBufferPointer();
	SIZE_T root_signature_size = blob->GetBufferSize();

	if (FAILED(gDevice->CreateRootSignature(0, root_signature_pointer, root_signature_size, IID_PPV_ARGS(&ioShader.mData.mRootSignature))))
		return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc = {};
	pipeline_state_desc.CS.pShaderBytecode = blob->GetBufferPointer();
	pipeline_state_desc.CS.BytecodeLength = blob->GetBufferSize();
	pipeline_state_desc.pRootSignature = ioShader.mData.mRootSignature.Get();
	if (FAILED(gDevice->CreateComputePipelineState(&pipeline_state_desc, IID_PPV_ARGS(&ioShader.mData.mPipelineState))))
		return false;

	std::wstring name = gToWString(inCSName);
	ioShader.mData.mPipelineState->SetName(name.c_str());

	gInspectShaderReflection(blob, ioShader);

	return true;
}

bool gCreateLibPipelineState(const char* inShaderFileName, const wchar_t* inLibName, Shader& ioShader)
{
	ComPtr<IDxcBlob> blob = gCompileShader(inShaderFileName, "", "lib_6_6");
	if (blob == nullptr)
		return false;

	if (ioShader.mRootSignatureReference == nullptr || ioShader.mRootSignatureReference->mData.mRootSignature == nullptr)
		return false;

	std::vector<const wchar_t*> hit_shader_names;
	ComPtr<ID3D12StateObject> pipeline_object = gCreateStateObject(blob.Get(), ioShader);
	if (pipeline_object == nullptr)
		return false;

	ioShader.mData.mRootSignature = ioShader.mRootSignatureReference->mData.mRootSignature;
	ioShader.mData.mStateObject = pipeline_object;
	ioShader.mData.mStateObject->SetName(inLibName);

	gInspectLibraryReflection(blob, ioShader);

	return true;
}

bool gCreatePipelineState(Shader& ioShader)
{
	if (ioShader.mRayGenerationName != nullptr)
		return gCreateLibPipelineState(ioShader.mFileName, ioShader.mRayGenerationName, ioShader);
	else if (ioShader.mMissName != nullptr)
		return gCreateLibPipelineState(ioShader.mFileName, ioShader.mMissName, ioShader);
	else if (ioShader.HitName() != nullptr)
		return gCreateLibPipelineState(ioShader.mFileName, ioShader.HitName(), ioShader);
	else if (ioShader.mCSName != nullptr)
		return gCreateCSPipelineState(ioShader.mFileName, ioShader.mCSName, ioShader);
	else
		return gCreateVSPSPipelineState(ioShader.mFileName, ioShader.mVSName, ioShader.mPSName, ioShader);
}