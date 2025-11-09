#pragma once

#include "Shared.h"

#define PNANOVDB_HLSL
#include "nanovdb/PNanoVDB.h"

struct NanoVDBContext
{
	void Initialize(NanoVDBInfo inInfo)
	{
		mValid									= inInfo.mBufferIndex != (uint)ViewDescriptorIndex::Invalid;
		if (!mValid)
			return;

		mInfo									= inInfo;

		StructuredBuffer<uint> buf				= ResourceDescriptorHeap[mInfo.mBufferIndex];

		// PrepareVdbVolume, https://github.com/eidosmontreal/unreal-vdb/blob/main/Shaders/Private/VdbToVolume.usf
		pnanovdb_address_t Address;				Address.byte_offset = 0;
		pnanovdb_grid_handle_t Grid;			Grid.address = Address;
		pnanovdb_tree_handle_t Tree				= pnanovdb_grid_get_tree(buf, Grid);
		pnanovdb_root_handle_t Root				= pnanovdb_tree_get_root(buf, Tree);

		pnanovdb_readaccessor_init(acc, Root);
	}

	float SampleCoords(uint3 inCoords)
	{
		StructuredBuffer<uint> buf				= ResourceDescriptorHeap[mInfo.mBufferIndex];

		pnanovdb_coord_t ijk					= mInfo.mOffset + inCoords;

		// ReadValue, https://github.com/eidosmontreal/unreal-vdb/blob/main/Shaders/Private/VdbCommon.ush, adjusted
		pnanovdb_uint32_t grid_type				= 1; // float
		pnanovdb_address_t address				= pnanovdb_readaccessor_get_value_address(grid_type, buf, acc, ijk);
		float density							= pnanovdb_read_float(buf, address);
		return									density;
	}

	float Sample(float3 inPositionOS)
	{
		// [TODO] Use [-1,1] Cube as NanoVDB container for now, convert to [0,1]
		float3 normalized_coords				= (inPositionOS + 1.0) * 0.5;
		uint3 uint_coords						= uint3(normalized_coords * mInfo.mSize);

#if NANOVDB_USE_TEXTURE
		{
			Texture3D<float> Texture			= ResourceDescriptorHeap[mInfo.mTextureIndex];
			return								Texture[uint_coords];
		}
#endif // NANOVDB_USE_TEXTURE

		return SampleCoords(uint_coords);
	}

	pnanovdb_readaccessor_t acc;
	NanoVDBInfo mInfo;
	bool mValid;
};

