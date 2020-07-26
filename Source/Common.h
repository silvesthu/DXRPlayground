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

#include "Thirdparty/glm/glm/gtx/transform.hpp"

// System
extern ID3D12Device5*						gDevice;
extern ID3D12DescriptorHeap* 				gRtvDescHeap;
extern ID3D12CommandQueue* 					gCommandQueue;
extern ID3D12GraphicsCommandList4* 			gCommandList;

extern ID3D12Fence* 						gIncrementalFence;			// Fence value increment each frame (most time)
extern HANDLE                       		gIncrementalFenceEvent;		// Allow CPU to wait on fence
extern uint64_t                       		gFenceLastSignaledValue;

extern IDXGISwapChain3* 					gSwapChain;
extern HANDLE                       		gSwapChainWaitableObject;
extern ID3D12Resource*						gBackBufferRenderTargetResource[];
extern D3D12_CPU_DESCRIPTOR_HANDLE			gBackBufferRenderTargetDescriptor[];

// ImGui
extern ID3D12DescriptorHeap* 				gImGuiSrvDescHeap;

// Application
struct ShaderTable
{
	ID3D12Resource* mResource = nullptr;
	uint64_t								mEntrySize = 0;
	uint32_t								mRayGenOffset = 0;
	uint32_t								mRayGenCount = 0;
	uint32_t								mMissOffset = 0;
	uint32_t								mMissCount = 0;
	uint32_t								mHitGroupOffset = 0;
	uint32_t								mHitGroupCount = 0;
};

struct InstanceData
{
	glm::vec3								mAlbedo = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3								mReflectance = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3								mEmission = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec1								mRoughness = glm::vec1(0.0f);
};

extern ID3D12RootSignature*					gDxrGlobalRootSignature;
extern ID3D12StateObject* 					gDxrStateObject;
extern ShaderTable							gDxrShaderTable;
extern ID3D12Resource*						gDxrOutputResource;
extern ID3D12DescriptorHeap*				gDxrDescriptorHeap;
extern ID3D12Resource*						gConstantGPUBuffer;

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

	uint64_t								mFenceValue;
};
extern FrameContext							gFrameContext[];
extern uint32_t								gFrameIndex;
extern float								gTime;

enum class DebugMode : uint32_t
{
	None = 0,
	Barycentrics,

	Count
};

enum class ShadowMode : uint32_t
{
	None = 0,
	Test,

	Count
};

struct PerFrame
{
	glm::vec4								mBackgroundColor	= glm::vec4(0.4f, 0.6f, 0.2f, 1.0f);
	glm::vec4								mCameraPosition		= glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	glm::vec4								mCameraDirection	= glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
	glm::vec4								mCameraRightExtend	= glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec4								mCameraUpExtend		= glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

	DebugMode								mDebugMode			= DebugMode::None;
	ShadowMode								mShadowMode			= ShadowMode::None;
};
extern PerFrame								gPerFrameConstantBuffer;

// String literals
static const wchar_t*						kDefaultRayGenerationShader	= L"defaultRayGeneration";
static const wchar_t*						kDefaultMissShader			= L"defaultMiss";
static const wchar_t*						kDefaultClosestHitShader	= L"defaultClosestHit";
static const wchar_t*						kDefaultHitGroup			= L"defaultHitGroup";
static const wchar_t*						kShadowMissShader			= L"shadowMiss";
static const wchar_t*						kShadowClosestHitShader		= L"shadowClosestHit";
static const wchar_t*						kShadowHitGroup				= L"shadowHitGroup";

// Helper
template <typename T>
inline T gAlignUp(T inValue, T inAlignment)
{
	return (((inValue + inAlignment - 1) / inAlignment) * inAlignment);
}

template <typename T>
inline T gMin(T inLhs, T inRhs)
{
	return inLhs < inRhs ? inLhs : inRhs;
}

template <typename T>
inline T gMax(T inLhs, T inRhs)
{
	return inLhs > inRhs ? inLhs : inRhs;
}

inline void gDebugPrint(const char* inString)
{
	OutputDebugStringA(inString);
}

template <typename T>
inline void gDebugPrint(const T& in)
{
	std::string str = std::to_string(in) + "\n";
	OutputDebugStringA(str.c_str());
}

// DirectX helper
template <typename T>
inline void gSafeRelease(T*& inPointer)
{
	if (inPointer != nullptr)
	{
		inPointer->Release();
		inPointer = nullptr;
	}
}

inline void gSafeCloseHandle(HANDLE& inHandle)
{
	if (inHandle != nullptr)
	{
		CloseHandle(inHandle);
		inHandle = nullptr;
	}
}

inline void gValidate(HRESULT in)
{
	if (FAILED(in))
	{
		char message[512];
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, in, 0, message, ARRAYSIZE(message), nullptr);
		OutputDebugStringA(message);
		assert(false);
	}
}

template <typename T>
inline void gSetName(ComPtr<T>& inObject, std::wstring inBaseName, std::wstring inName)
{
	std::wstring new_name = inBaseName + inName;
	inObject->SetName(new_name.c_str());
}

inline void gBarrierTransition(ID3D12GraphicsCommandList4* inCommandList, ID3D12Resource* inResource, D3D12_RESOURCE_STATES inBefore, D3D12_RESOURCE_STATES inAfter)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = inResource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = inBefore;
	barrier.Transition.StateAfter = inAfter;
	inCommandList->ResourceBarrier(1, &barrier);
}

inline void gBarrierUAV(ID3D12GraphicsCommandList4* inCommandList, ID3D12Resource* inResource)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = inResource;
	inCommandList->ResourceBarrier(1, &barrier);
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

inline D3D12_RESOURCE_DESC gGetBufferResourceDesc(UINT64 inWidth)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Alignment = 0;
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.Width = inWidth;
	desc.Height = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	return desc;
}

inline D3D12_RESOURCE_DESC gGetTextureResourceDesc(UINT64 inWidth, UINT inHeight, DXGI_FORMAT inFormat)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Format = inFormat;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	desc.Width = inWidth;
	desc.Height = inHeight;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	return desc;
}

inline D3D12_RESOURCE_DESC gGetUAVResourceDesc(UINT64 inWidth)
{
	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(inWidth);
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	return desc;
}