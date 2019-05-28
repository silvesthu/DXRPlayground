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

#include "Thirdparty/glm/glm/gtx/transform.hpp"

enum 
{
	NUM_FRAMES_IN_FLIGHT = 3,
	NUM_BACK_BUFFERS = 3
};

struct FrameContext
{
	ID3D12CommandAllocator*					CommandAllocator;
	uint64_t									FenceValue;
};

extern FrameContext							gFrameContext[];
extern uint32_t								gFrameIndex;

extern ID3D12Device5*						gD3DDevice;
extern ID3D12DescriptorHeap* 				gD3DRtvDescHeap;
extern ID3D12DescriptorHeap* 				gImGuiSrvDescHeap;
extern ID3D12CommandQueue* 					gD3DCommandQueue;
extern ID3D12GraphicsCommandList4* 			gD3DCommandList;
extern ID3D12Fence* 						gFence;
extern HANDLE                       		gFenceEvent;
extern uint64_t                       		gFenceLastSignaledValue;
extern IDXGISwapChain3* 					gSwapChain;
extern HANDLE                       		gSwapChainWaitableObject;
extern ID3D12Resource*						gBackBufferRenderTargetResource[];
extern D3D12_CPU_DESCRIPTOR_HANDLE			gBackBufferRenderTargetDescriptor[];

extern ID3D12Resource* 						gDxrVertexBuffer;
extern ID3D12Resource* 						gDxrBottomLevelAccelerationStructureScratch;
extern ID3D12Resource* 						gDxrBottomLevelAccelerationStructureDest;
extern ID3D12Resource* 						gDxrTopLevelAccelerationStructureScratch;
extern ID3D12Resource* 						gDxrTopLevelAccelerationStructureDest;
extern ID3D12Resource* 						gDxrTopLevelAccelerationStructureInstanceDesc;

extern ID3D12RootSignature*					gDxrEmptyRootSignature;
extern ID3D12StateObject* 					gDxrStateObject;
extern ID3D12Resource*						gDxrShaderTable;
extern uint64_t								gDxrShaderTableEntrySize;
extern ID3D12Resource*						gDxrOutputResource;
extern ID3D12Resource*						gDxrConstantBufferResource;
extern ID3D12DescriptorHeap*				gDxrCbvSrvUavHeap;
extern ID3D12Resource*						gDxrHitConstantBufferResource;

struct PerFrame
{
	float mBackgroundColor[4] = { 0.4f, 0.6f, 0.2f, 1.0f };
};
extern PerFrame								gPerFrameConstantBuffer;

// String literals
static const wchar_t*						kRayGenShader		= L"rayGen";
static const wchar_t*						kMissShader			= L"miss";
static const wchar_t*						kClosestHitShader	= L"chs";
static const wchar_t*						kHitGroup			= L"HitGroup";

// Helper
template <typename T>
void gSafeRelease(T*& pointer)
{
	if (pointer != nullptr)
	{
		pointer->Release();
		pointer = nullptr;
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