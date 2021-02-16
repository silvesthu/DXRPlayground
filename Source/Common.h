#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxcapi.h>
#include <dxgidebug.h>
#include <comdef.h>
#include <wrl.h>	// For ComPtr. See https://github.com/Microsoft/DirectXTK/wiki/ComPtr
using Microsoft::WRL::ComPtr;

#include <tchar.h>
#include <string>
#include <iostream>
#include <array>
#include <vector>
#include <memory>
#include <functional>
#include <type_traits>
#include <locale>
#include <codecvt>
#include <filesystem>

#include "Thirdparty/glm/glm/gtx/transform.hpp"
#include "Thirdparty/nameof/include/nameof.hpp"
#include "Thirdparty/DirectXTex/DirectXTex/DirectXTex.h"
#include "ImGui/imgui_impl_helper.h"

// System
extern ID3D12Device5*						gDevice;
extern ID3D12DescriptorHeap* 				gRTVDescriptorHeap;
extern ID3D12CommandQueue* 					gCommandQueue;
extern ID3D12GraphicsCommandList4* 			gCommandList;

extern ID3D12Fence* 						gIncrementalFence;			// Fence value increment each frame (most time)
extern HANDLE                       		gIncrementalFenceEvent;		// Allow CPU to wait on fence
extern uint64_t                       		gFenceLastSignaledValue;

extern IDXGISwapChain3* 					gSwapChain;
extern HANDLE                       		gSwapChainWaitableObject;
extern ID3D12Resource*						gBackBufferRenderTargetResource[];
extern D3D12_CPU_DESCRIPTOR_HANDLE			gBackBufferRenderTargetRTV[];

// Application
struct ShaderTable
{
	ID3D12Resource* mResource = nullptr;
	glm::uint64								mEntrySize = 0;
	glm::uint32								mRayGenOffset = 0;
	glm::uint32								mRayGenCount = 0;
	glm::uint32								mMissOffset = 0;
	glm::uint32								mMissCount = 0;
	glm::uint32								mHitGroupOffset = 0;
	glm::uint32								mHitGroupCount = 0;
};

extern ComPtr<ID3D12Resource>				gConstantGPUBuffer;

extern ComPtr<ID3D12RootSignature>			gDXRGlobalRootSignature;
extern ComPtr<ID3D12StateObject>			gDXRStateObject;
extern ShaderTable							gDXRShaderTable;

#define MEMBER(parent_type, type, name, default_value) \
	parent_type& name(type in##name) { m##name = in##name; return *this; } \
	type m##name = default_value;

struct Shader
{
#define SHADER_MEMBER(type, name, default_value) MEMBER(Shader, type, name, default_value)

	SHADER_MEMBER(const wchar_t*, VSName, nullptr);
	SHADER_MEMBER(const wchar_t*, PSName, nullptr);
	SHADER_MEMBER(const wchar_t*, CSName, nullptr);

	struct DescriptorEntry
	{
		DescriptorEntry(ID3D12Resource* inResource) { mResource = inResource; }
		DescriptorEntry(D3D12_GPU_VIRTUAL_ADDRESS inAddress) { mAddress = inAddress; }

		ID3D12Resource* mResource = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS mAddress = 0;
	};

	void InitializeDescriptors(const std::vector<Shader::DescriptorEntry>& inEntries);
	void SetupGraphics();
	void SetupCompute();
	void Reset() { mData = {}; }

	struct Data
	{
		ComPtr<ID3D12RootSignatureDeserializer> mRootSignatureDeserializer;
		ComPtr<ID3D12RootSignature> mRootSignature;
		ComPtr<ID3D12PipelineState> mPipelineState;
		ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
	};
	Data mData;
};

struct Texture
{
#define TEXTURE_MEMBER(type, name, default_value) MEMBER(Texture, type, name, default_value)

	TEXTURE_MEMBER(glm::uint32, Width, 1);
	TEXTURE_MEMBER(glm::uint32, Height, 1);
	TEXTURE_MEMBER(glm::uint32, Depth, 1);
	TEXTURE_MEMBER(DXGI_FORMAT, Format, DXGI_FORMAT_R32G32B32A32_FLOAT);
	TEXTURE_MEMBER(const char*, Name, nullptr);
	TEXTURE_MEMBER(float, UIScale, 1.0f);

	void Initialize();

	ComPtr<ID3D12Resource> mResource;
	D3D12_CPU_DESCRIPTOR_HANDLE mCPUHandle = {};
	D3D12_GPU_DESCRIPTOR_HANDLE mGPUHandle = {};
};

extern Shader								gCompositeShader;

// Frame
enum
{
	NUM_FRAMES_IN_FLIGHT = 3,
	NUM_BACK_BUFFERS = 3
};
struct FrameContext
{
	ID3D12CommandAllocator*					mCommandAllocator		= nullptr;

	ID3D12Resource*							mConstantUploadBuffer	= nullptr;
	void*									mConstantUploadBufferPointer = nullptr;

	glm::uint64								mFenceValue;
};
extern FrameContext							gFrameContext[];
extern glm::uint32							gFrameIndex;
extern glm::float32							gTime;

extern Texture*								gDumpTexture;
extern Texture								gDumpTextureProxy;

enum class DebugMode : glm::uint32
{
	None = 0,

	_Newline0,

	Barycentrics,
	Vertex,
	Normal,

	_Newline1,

	Albedo,
	Reflectance,
	Emission,
	Roughness,

	_Newline2,

	Transmittance,
	InScattering,

	_Newline3,

	RecursionCount,

	Count
};

enum class DebugInstanceMode : glm::uint32
{
	None = 0,

	Barycentrics,
	Mirror,

	Count
};

enum class BackgroundMode : glm::uint32
{
	Color = 0,

	RaymarchAtmosphereOnly,
	PrecomputedAtmosphere,

	Count
};

namespace ShaderType
{
	using float2 = glm::vec2;
	using float3 = glm::vec3;
	using float4 = glm::vec4;

	using uint = glm::uint;
	using uint2 = glm::uvec2;
	using uint3 = glm::uvec3;
	using uint4 = glm::uvec4;

	#define HLSL_AS_CPP
	#define CONSTANT_DEFAULT(x) =x
	const static float MATH_PI = glm::pi<float>();
	#include "../Shader/ShaderType.hlsl"
};
extern ShaderType::PerFrame					gPerFrameConstantBuffer;

// String literals
static const wchar_t*						kDefaultRayGenerationShader	= L"DefaultRayGeneration";
static const wchar_t*						kDefaultMissShader			= L"DefaultMiss";
static const wchar_t*						kDefaultClosestHitShader	= L"DefaultClosestHit";
static const wchar_t*						kDefaultHitGroup			= L"DefaultHitGroup";
static const wchar_t*						kShadowMissShader			= L"ShadowMiss";
static const wchar_t*						kShadowClosestHitShader		= L"ShadowClosestHit";
static const wchar_t*						kShadowHitGroup				= L"ShadowHitGroup";

// Helper
template <typename T>
inline T gAlignUp(T value, T alignment)
{
	return (((value + alignment - 1) / alignment) * alignment);
}

template <typename T>
inline T gMin(T lhs, T rhs)
{
	return lhs < rhs ? lhs : rhs;
}

template <typename T>
inline T gMax(T lhs, T rhs)
{
	return lhs > rhs ? lhs : rhs;
}

inline void gDebugPrint(const char* string)
{
	OutputDebugStringA(string);
	gLog.AddLog(string);
}

template <typename T>
inline void gDebugPrint(const T& data)
{
	std::string str = std::to_string(data) + "\n";
	OutputDebugStringA(str.c_str());
	gLog.AddLog(str.c_str());
}

inline std::wstring gToWString(const std::string string)
{
	int wide_size = MultiByteToWideChar(CP_UTF8, 0, string.c_str(), (int)string.size(), NULL, 0);
	std::wstring wide_string(wide_size, 0);
	MultiByteToWideChar(CP_UTF8, 0, string.c_str(), (int)string.size(), &wide_string[0], wide_size);
	return wide_string;
}

namespace nameof
{
	template <typename T>
	constexpr std::string_view nameof_enum_type() noexcept
	{
		return nameof::nameof_type<T>().substr(5);
	}
}

// DirectX helper
template <typename T>
inline void gSafeRelease(T*& pointer)
{
	if (pointer != nullptr)
	{
		pointer->Release();
		pointer = nullptr;
	}
}

inline void gSafeCloseHandle(HANDLE& handle)
{
	if (handle != nullptr)
	{
		CloseHandle(handle);
		handle = nullptr;
	}
}

inline void gValidate(HRESULT hr)
{
	if (FAILED(hr))
	{
		char message[512];
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, hr, 0, message, ARRAYSIZE(message), nullptr);
		OutputDebugStringA(message);
		assert(false);
	}
}

template <typename T>
inline void gSetName(ComPtr<T>& object, std::wstring base_name, std::wstring name)
{
	std::wstring new_name = base_name + name;
	object->SetName(new_name.c_str());
}

inline void gBarrierTransition(ID3D12GraphicsCommandList4* command_list, ID3D12Resource* resource, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = state_before;
	barrier.Transition.StateAfter = state_after;
	command_list->ResourceBarrier(1, &barrier);
}

inline void gBarrierUAV(ID3D12GraphicsCommandList4* command_list, ID3D12Resource* resource)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = resource;
	command_list->ResourceBarrier(1, &barrier);
}

inline D3D12_HEAP_PROPERTIES gGetDefaultHeapProperties()
{
	D3D12_HEAP_PROPERTIES props = {};
	props.Type = D3D12_HEAP_TYPE_DEFAULT;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	return props;
}

inline D3D12_HEAP_PROPERTIES gGetUploadHeapProperties()
{
	D3D12_HEAP_PROPERTIES props = {};
	props.Type = D3D12_HEAP_TYPE_UPLOAD;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	return props;
}

inline D3D12_RESOURCE_DESC gGetBufferResourceDesc(UINT64 width)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Width = width;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	return desc;
}

inline D3D12_RESOURCE_DESC gGetTextureResourceDesc(UINT width, UINT height, UINT depth, DXGI_FORMAT format)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.DepthOrArraySize = (UINT16)depth;
	desc.Dimension = depth == 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE2D : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	desc.Format = format;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	desc.Width = width;
	desc.Height = height;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	return desc;
}

inline D3D12_RESOURCE_DESC gGetUAVResourceDesc(UINT64 width)
{
	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(width);
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	return desc;
}
