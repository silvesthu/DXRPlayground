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

enum 
{
	NUM_FRAMES_IN_FLIGHT = 3,
	NUM_BACK_BUFFERS = 3
};

struct FrameContext
{
	ID3D12CommandAllocator*					CommandAllocator;
	uint64_t								FenceValue;
};

extern FrameContext							gFrameContext[];
extern uint32_t								gFrameIndex;
extern float								gTime;

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

struct VertexBuffer
{
	ID3D12Resource* mResource = nullptr;
	uint32_t mVertexCount = 0;
	uint32_t mVertexSize = 0;

	void Release() { gSafeRelease(mResource); }
};
extern VertexBuffer 						gDxrTriangleVertexBuffer;
extern VertexBuffer							gDxrPlaneVertexBuffer;

struct BottomLevelAccelerationStructure
{
	ID3D12Resource* mScratch = nullptr;
	ID3D12Resource* mDest = nullptr;

	void Release() { gSafeRelease(mScratch); gSafeRelease(mDest); }
};
extern BottomLevelAccelerationStructure 	gDxrTriangleBLAS;
extern BottomLevelAccelerationStructure 	gDxrPlaneBLAS;

extern ID3D12Resource* 						gDxrTopLevelAccelerationStructureScratch;
extern ID3D12Resource* 						gDxrTopLevelAccelerationStructureDest;
extern ID3D12Resource* 						gDxrTopLevelAccelerationStructureInstanceDesc;

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
static const wchar_t*						kTriangleHitShader	= L"triangleHit";
static const wchar_t*						kPlaneHitShader		= L"planeHit";
static const wchar_t*						kTriangleHitGroup	= L"TriangleHitGroup";
static const wchar_t*						kPlaneHitGroup		= L"PlaneHitGroup";
static const wchar_t*						kShadowHitShader	= L"shadowHit";
static const wchar_t*						kShadowMissShader	= L"shadowMiss";
static const wchar_t*						kShadowHitGroup		= L"shadowGroup";