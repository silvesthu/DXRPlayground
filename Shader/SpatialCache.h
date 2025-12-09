#pragma once

namespace SpatialCache
{
    //https://www.shadertoy.com/view/XlGcRh
    uint pcg(uint v)
    {
        uint state = v * 747796405u + 2891336453u;
        uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    //https://www.shadertoy.com/view/XlGcRh
    uint xxhash32(uint p)
    {
        const uint PRIME32_2 = 2246822519U, PRIME32_3 = 3266489917U;
        const uint PRIME32_4 = 668265263U, PRIME32_5 = 374761393U;
        uint h32 = p + PRIME32_5;
        h32 = PRIME32_4 * ((h32 << 17) | (h32 >> (32 - 17)));
        h32 = PRIME32_2 * (h32 ^ (h32 >> 15));
        h32 = PRIME32_3 * (h32 ^ (h32 >> 13));
        return h32 ^ (h32 >> 16);
    }

    static const uint HASHMAP_SIZE = kSpatialHashSize;
    static const uint SEARCH_COUNT = 16u;

	static const uint kInvalidCellIndex = 0xFFFFFFFFu;

    static const float kCellSize = 0.05;

    // https://interplayoflight.wordpress.com/2025/11/23/spatial-hashing-for-raytraced-ambient-occlusion/
    //Adapted from https://gboisse.github.io/posts/this-is-us/
    uint FindOrInsert(float3 position, float3 normal, float cellSize)
    {
        // Inputs to hashing
        int3 p = floor(position / cellSize + 1E-3 /* Z-fighting in CornellBox */);
        int3 n = floor(normal * 3.0);

        cellSize *= 10000; // cellSize can be small and lead to more conflicts, multiply to increase range

        uint hashKey = pcg(cellSize + pcg(p.x + pcg(p.y + pcg(p.z + pcg(n.x + pcg(n.y + pcg(n.z)))))));

        uint cellIndex = hashKey % HASHMAP_SIZE;

        uint checksum = xxhash32(cellSize + xxhash32(p.x + xxhash32(p.y + xxhash32(p.z + xxhash32(n.x + xxhash32(n.y + xxhash32(n.z)))))));
        checksum = max(checksum, 1); // 0 is reserved for available cells

        // Update data structure
        for (uint i = 0; i < SEARCH_COUNT; i++)
        {
            uint cmp;
            InterlockedCompareExchange(SpatialHashUAV[cellIndex], 0, checksum, cmp);

            if (cmp == 0 || cmp == checksum)
            {
                return cellIndex;
            }

            cellIndex++;

            if (cellIndex >= HASHMAP_SIZE)
                break;
        }

        return kInvalidCellIndex; // out of memory 
    }

    uint LoadData(uint inCellIndex)
    {
		if (inCellIndex == kInvalidCellIndex)
            return 0;

		return SpatialDataUAV[inCellIndex];
    }

    uint AddData(uint inCellIndex, uint inValue)
    {
        if (inCellIndex == kInvalidCellIndex)
            return 0;

        uint original_value;
        InterlockedAdd(SpatialDataUAV[inCellIndex], inValue, original_value);
        return original_value;
    }

    // SHARC
    // Debug functions
    float3 HashGridGetColorFromHash32(uint hash)
    {
        float3 color;
        color.x = ((hash >> 0) & 0x3ff) / 1023.0f;
        color.y = ((hash >> 11) & 0x7ff) / 2047.0f;
        color.z = ((hash >> 22) & 0x7ff) / 2047.0f;

        return color;
    }
}