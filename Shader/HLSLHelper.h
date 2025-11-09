#pragma once

// Dummies to make INTELLISENSE happy
#ifdef __INTELLISENSE__

#define in
#define out
#define inout
#define cbuffer struct
#define groupshared
template <typename T> struct ConstantBuffer : public T {};
template <typename T> struct StructuredBuffer {};
template <typename T> struct RWStructuredBuffer {};
template <typename T> struct Texture2D {};
template <typename T> struct Texture3D {};
template <typename T> struct RWTexture2D {};
template <typename T> struct RWTexture3D {};
struct SamplerState {};
typedef unsigned int uint;
uint ResourceDescriptorHeap[];
uint SamplerDescriptorHeap[];

template <typename T> T normalize(T a) { return a; }
template <typename T> T abs(T a) { return a; }
template <typename T> T log(T a) { return a; }
template <typename T> T exp(T a) { return a; }
template <typename T1, typename T2> T1 mul(T1 a, T2 b) { return a; }
template <typename T1, typename T2> T1 min(T1 a, T2 b) { return a; }
template <typename T1, typename T2> T1 max(T1 a, T2 b) { return a; }
template <typename T1, typename T2> T1 dot(T1 a, T2 b) { return a; }
template <typename T1, typename T2, typename T3> T1 lerp(T1 a, T2 b, T3 c) { return a; }
template <typename T1, typename T2, typename T3> T1 clamp(T1 a, T2 b, T3 c) { return a; }

struct RaytracingAccelerationStructure {};
struct RayDesc
{
    float3 Origin;
    float  TMin;
    float3 Direction;
    float  TMax;
};

enum RAY_FLAG : uint
{
    RAY_FLAG_NONE = 0x00,
    RAY_FLAG_FORCE_OPAQUE = 0x01,
    RAY_FLAG_FORCE_NON_OPAQUE = 0x02,
    RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH = 0x04,
    RAY_FLAG_SKIP_CLOSEST_HIT_SHADER = 0x08,
    RAY_FLAG_CULL_BACK_FACING_TRIANGLES = 0x10,
    RAY_FLAG_CULL_FRONT_FACING_TRIANGLES = 0x20,
    RAY_FLAG_CULL_OPAQUE = 0x40,
    RAY_FLAG_CULL_NON_OPAQUE = 0x80,
    RAY_FLAG_SKIP_TRIANGLES = 0x100,
    RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES = 0x200,
};

enum COMMITTED_STATUS : uint
{
    COMMITTED_NOTHING,
    COMMITTED_TRIANGLE_HIT,
    COMMITTED_PROCEDURAL_PRIMITIVE_HIT
};

template <RAY_FLAG RayFlags>
struct RayQuery 
{
	RAY_FLAG RayFlags() { return RAY_FLAG_NONE; }
	COMMITTED_STATUS CommittedStatus() { return COMMITTED_NOTHING; }
	uint CommittedInstanceID() { return 0; }
	float CommittedRayT() { return 0.0f; }
};

#define SHADER_DEBUG 1
#define USE_HALF 1
#define USE_TEXTURE 1
#define NVAPI_LSS 1
#define NVAPI_SER 1

#define __SHADER_TARGET_MAJOR 6
#define __SHADER_TARGET_MINOR 7

#endif // __INTELLISENSE__
