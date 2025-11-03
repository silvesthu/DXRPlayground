#pragma once

#include "Shared.h"
#include "HLSL.h"

#define PNANOVDB_HLSL
#include "nanovdb/PNanoVDB.h"

float SampleNanoVDBCoords(uint3 inCoords, NanoVDBInfo inInfo)
{
	StructuredBuffer<uint> buf				= ResourceDescriptorHeap[inInfo.mBufferIndex];
	pnanovdb_coord_t ijk					= inInfo.mOffset + inCoords;

	// PrepareVdbVolume, https://github.com/eidosmontreal/unreal-vdb/blob/main/Shaders/Private/VdbToVolume.usf
	pnanovdb_address_t Address;				Address.byte_offset = 0;
	pnanovdb_grid_handle_t Grid;			Grid.address = Address;
	pnanovdb_tree_handle_t Tree				= pnanovdb_grid_get_tree(buf, Grid);
	pnanovdb_root_handle_t Root				= pnanovdb_tree_get_root(buf, Tree);
	pnanovdb_uint32_t grid_type				= pnanovdb_grid_get_grid_type(buf, Grid);

	pnanovdb_readaccessor_t acc;
	pnanovdb_readaccessor_init(acc, Root);

	// ReadValue, https://github.com/eidosmontreal/unreal-vdb/blob/main/Shaders/Private/VdbCommon.ush, adjusted
	pnanovdb_uint32_t level;
	pnanovdb_address_t address				= pnanovdb_readaccessor_get_value_address(grid_type, buf, acc, ijk);
	float density							= pnanovdb_read_float(buf, address);
	return									density;
}

float SampleNanoVDB(float3 inPositionOS, NanoVDBInfo inInfo)
{
	// [TODO] Use [-1,1] Cube as NanoVDB container for now, convert to [0,1]
	float3 normalized_coords				= (inPositionOS + 1.0) * 0.5;
	uint3 uint_coords						= uint3(normalized_coords * inInfo.mSize);

	return SampleNanoVDBCoords(uint_coords, inInfo);
}
