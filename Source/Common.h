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

// Helper
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

extern ID3D12DescriptorHeap* 				gImGuiSrvDescHeap;

extern ID3D12RootSignature*					gDxrEmptyRootSignature;
extern ID3D12StateObject* 					gDxrStateObject;

struct ShaderTable
{
	ID3D12Resource*							mResource = nullptr;
	uint64_t								mEntrySize = 0;
	uint32_t								mRayGenOffset = 0;
	uint32_t								mRayGenCount = 0;
	uint32_t								mMissOffset = 0;
	uint32_t								mMissCount = 0;
	uint32_t								mHitGroupOffset = 0;
	uint32_t								mHitGroupCount = 0;
};
extern ShaderTable							gDxrShaderTable;

extern ID3D12Resource*						gDxrOutputResource;
extern ID3D12DescriptorHeap*				gDxrCbvSrvUavHeap;
extern ID3D12Resource*						gDxrHitConstantBufferResource;

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

struct PerFrame
{
	glm::vec4 mBackgroundColor				= glm::vec4(0.4f, 0.6f, 0.2f, 1.0f);
	glm::vec4 mCameraPosition				= glm::vec4(0.0f, 0.0f, -5.0f, 0.0f);
	glm::vec4 mCameraDirection				= glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
	glm::vec4 mCameraRightExtend			= glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec4 mCameraUpExtend				= glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
};
extern PerFrame								gPerFrameConstantBuffer;

// String literals
static const wchar_t*						kRayGenShader		= L"rayGen";
static const wchar_t*						kMissShader			= L"miss";
static const wchar_t*						kTriangleHitShader	= L"triangleHit";
static const wchar_t*						kPlaneHitShader		= L"planeHit";
static const wchar_t*						kTriangleHitGroup	= L"TriangleHitGroup";
static const wchar_t*						kPlaneHitGroup		= L"PlaneHitGroup";
static const wchar_t*						kShadowHitShader	= L"shadowHit";
static const wchar_t*						kShadowMissShader	= L"shadowMiss";
static const wchar_t*						kShadowHitGroup		= L"shadowGroup";