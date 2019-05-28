#include "AccelerationStructure.h"
#include "Common.h"

void CreateShaderTable()
{
	/** The shader-table layout is as follows:
        Entry 0 - Ray-gen program
        Entry 1 - Miss program
        Entry 2 - Hit program
        All entries in the shader-table must have the same size, so we will choose it base on the largest required entry.
        The ray-gen program requires the largest entry - sizeof(program identifier) + 8 bytes for a descriptor-table.
        The entry size must be aligned up to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT
    */

	// Create the table
	{
		// Calculate the size and create the buffer
		gDxrShaderTableEntrySize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

		gDxrShaderTableEntrySize += sizeof(D3D12_GPU_VIRTUAL_ADDRESS); // depends on local root argument

		gDxrShaderTableEntrySize = gAlignUp(gDxrShaderTableEntrySize, (uint64_t)D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

		uint64_t shaderTableSize = gDxrShaderTableEntrySize * 3;

		// For simplicity, we create the shader-table on the upload heap. You can also create it on the default heap
		D3D12_HEAP_PROPERTIES heap_props;
		heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
		heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_props.CreationNodeMask = 0;
		heap_props.VisibleNodeMask = 0;

		// Resource description
		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Alignment = 0;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.Height = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.MipLevels = 1;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.SampleDesc.Quality = 0;
		bufDesc.Width = shaderTableSize;

		gValidate(gD3DDevice->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gDxrShaderTable)));
		gDxrShaderTable->SetName(L"gDxrShaderTable");
	}
 
	// Fill the table
	{
		// For now, shader table between all ray tracing shader types, by adding offset when reference the table
		// So it is necessary to align up to table size rather than record size
		// Use the record size when there are multiple entries of same shader type

		// Local root argument may be embedded along with shader identifier
		// https://github.com/NVIDIAGameWorks/DxrTutorials/blob/dcb8810086f80e77157a6a3b7deff2f24e0986d7/Tutorials/06-Raytrace/06-Raytrace.cpp#L734
		// https://github.com/NVIDIAGameWorks/Falcor/blob/236927c2bca252f9ea1e3bacb982f8fcba817a67/Framework/Source/Experimental/Raytracing/RtProgramVars.cpp#L116
		// p20 http://intro-to-dxr.cwyman.org/presentations/IntroDXR_RaytracingAPI.pdf
		// http://intro-to-dxr.cwyman.org/spec/DXR_FunctionalSpec_v0.09.docx

		// Signature -> DescriptorHeap
		// * Global - SetDescriptorHeaps() - D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE
		// * Per Shader -  SetDescriptorHeaps() - D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE
		// * Per Shader Instance - Shader Table (DescriptorHeap or raw data) - D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE

		// Map
		uint8_t* data_pointer;
		gValidate(gDxrShaderTable->Map(0, nullptr, (void**)& data_pointer));

		ComPtr<ID3D12StateObjectProperties> state_object_properties;
		gDxrStateObject->QueryInterface(IID_PPV_ARGS(&state_object_properties));

		memcpy(data_pointer + gDxrShaderTableEntrySize * 0, state_object_properties->GetShaderIdentifier(kRayGenShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		memcpy(data_pointer + gDxrShaderTableEntrySize * 1, state_object_properties->GetShaderIdentifier(kMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		memcpy(data_pointer + gDxrShaderTableEntrySize * 2, state_object_properties->GetShaderIdentifier(kHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		D3D12_GPU_VIRTUAL_ADDRESS constant_buffer_gpu_virtual_address = gDxrHitConstantBufferResource->GetGPUVirtualAddress();
		memcpy(data_pointer + gDxrShaderTableEntrySize * 2 + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, &constant_buffer_gpu_virtual_address, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));

		// Unmap
		gDxrShaderTable->Unmap(0, nullptr);
	}
}

void CleanupShaderTable()
{
	gSafeRelease(gDxrShaderTable);
}
