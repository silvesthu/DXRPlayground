#include "Common.h"

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

void gCreateShaderTable()
{
	// Construct the table
	ShaderTableEntry shader_table_entries[16];
	glm::uint32 shader_table_entry_index = 0;
	glm::uint32 shader_table_entry_count = 0;
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
		// * Per Shader -  SetDescriptorHeaps() - D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE
		// * Per Shader Instance - Shader Table (DescriptorHeap or raw address) - D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE

		ComPtr<ID3D12StateObjectProperties> state_object_properties;
		gDXRStateObject->QueryInterface(IID_PPV_ARGS(&state_object_properties));

		// RayGen shaders
		{
			gDXRShaderTable.mRayGenOffset = shader_table_entry_index;

			memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(kDefaultRayGenerationShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entry_index++;

			gDXRShaderTable.mRayGenCount = shader_table_entry_index - gDXRShaderTable.mRayGenOffset;
		}

		// Miss shaders
		{
			gDXRShaderTable.mMissOffset = shader_table_entry_index;

			memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(kDefaultMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entry_index++;

			memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(kShadowMissShader), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entry_index++;

			gDXRShaderTable.mMissCount = shader_table_entry_index - gDXRShaderTable.mMissOffset;
		}

		// HitGroup shaders - At least Material count * Ray(nth) count
		{
			gDXRShaderTable.mHitGroupOffset = shader_table_entry_index;

			memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(kDefaultHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entry_index++;

			memcpy(&shader_table_entries[shader_table_entry_index].mShaderIdentifier, state_object_properties->GetShaderIdentifier(kShadowHitGroup), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			shader_table_entry_index++;

			gDXRShaderTable.mHitGroupCount = shader_table_entry_index - gDXRShaderTable.mHitGroupOffset;
		}

		gDXRShaderTable.mEntrySize = sizeof(ShaderTableEntry);
		shader_table_entry_count = shader_table_entry_index;
	}

	// Create the table
	{
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gDXRShaderTable.mEntrySize * shader_table_entry_index);

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gDXRShaderTable.mResource)));
		gDXRShaderTable.mResource->SetName(L"gDxrShaderTable");
	}
 
	// Copy the table
	{
		// Map
		uint8_t* data_pointer;
		gValidate(gDXRShaderTable.mResource->Map(0, nullptr, (void**)& data_pointer));

		// Copy
		memcpy(data_pointer, shader_table_entries, gDXRShaderTable.mEntrySize * shader_table_entry_index);

		// Unmap
		gDXRShaderTable.mResource->Unmap(0, nullptr);
	}
}

void gCleanupShaderTable()
{
	gSafeRelease(gDXRShaderTable.mResource);
}
