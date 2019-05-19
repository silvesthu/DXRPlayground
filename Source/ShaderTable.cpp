#include "AccelerationStructure.h"
#include "CommonInclude.h"

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

	uint32_t shader_table_entry_size = 0;

	// Create the table
	{
		// Calculate the size and create the buffer
		shader_table_entry_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		shader_table_entry_size += 8; // The ray-gen's descriptor table
		shader_table_entry_size = gAlignUp((uint32_t)D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, shader_table_entry_size);
		uint32_t shaderTableSize = shader_table_entry_size * 3;

		// For simplicity, we create the shader-table on the upload heap. You can also create it on the default heap // ???
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
	}
 
	// Fill the table
	{
		// Map the buffer
		uint8_t* data_pointer;
		gValidate(gDxrShaderTable->Map(0, nullptr, (void**)& data_pointer));

		ComPtr<ID3D12StateObjectProperties> state_object_properties;
		gDxrStateObject->QueryInterface(IID_PPV_ARGS(&state_object_properties));

		// Entry 0 - ray-gen program ID and descriptor data
		memcpy(data_pointer, state_object_properties->GetShaderIdentifier(kRayGenShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
		uint64_t heapStart = gDxrSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;
		*(uint64_t*)(data_pointer + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) = heapStart;

		// Entry 1 - miss program
		memcpy(data_pointer + shader_table_entry_size, state_object_properties->GetShaderIdentifier(kMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		// Entry 2 - hit program
		uint8_t* pHitEntry = data_pointer + (uint64_t)shader_table_entry_size * 2; // +2 skips the ray-gen and miss entries
		memcpy(pHitEntry, state_object_properties->GetShaderIdentifier(kHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

		// Unmap
		gDxrShaderTable->Unmap(0, nullptr);
	}
}

void CleanupShaderTable()
{
	gSafeRelease(gDxrShaderTable);
}
