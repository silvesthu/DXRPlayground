#pragma once

#define WIN32_LEAN_AND_MEAN

#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxcapi.h>
#include <dxgidebug.h>
#include <comdef.h>
#include <wrl.h>	// For ComPtr. See https://github.com/Microsoft/DirectXTK/wiki/ComPtr
using Microsoft::WRL::ComPtr;
#include <pix3.h>

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
#include <span>
#include <execution>
#include <optional>

#include "Thirdparty/glm/glm/gtx/transform.hpp"
#include "Thirdparty/nameof/include/nameof.hpp"
#include "Thirdparty/DirectXTex/DirectXTex/DirectXTex.h"
#include "Thirdparty/DirectXTex/DirectXTex/d3dx12.h"
#include "ImGui/imgui_impl_helper.h"

#include "../Shader/Shared.inl"

// Common helpers
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

inline std::string gToLower(const std::string& inString)
{
	std::string result = inString;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

inline void gTrace(const char* inString)
{
	OutputDebugStringA(inString);
}

inline void gTrace(const std::string& inString)
{
	OutputDebugStringA(inString.c_str());
}

inline void gTrace(const std::string_view& inString)
{
	OutputDebugStringA(inString.data());
}

template <typename T>
inline void gTrace(const T& data)
{
	std::string str = std::to_string(data) + "\n";
	OutputDebugStringA(str.c_str());
}

inline void gDump(std::function<std::filesystem::path(const std::filesystem::path& inDirectory)> inWriteFileCallback, bool inOpenFile)
{
	std::filesystem::path directory = ".\\Dump\\";
	std::filesystem::create_directory(directory);

	std::filesystem::path path = inWriteFileCallback(directory);

	if (inOpenFile)
	{
		std::filesystem::path command = "explorer ";
		command += std::filesystem::current_path();
		command += "\\";
		command += path;
		system(command.string().c_str());
	}
}

#define gAssert assert

inline void gVerify(bool inExpr)
{
	if (!inExpr)
		__debugbreak();
}

inline std::wstring gToWString(const std::string_view string)
{
	int wide_size = MultiByteToWideChar(CP_UTF8, 0, string.data(), (int)string.size(), NULL, 0);
	std::wstring wide_string(wide_size, 0);
	MultiByteToWideChar(CP_UTF8, 0, string.data(), (int)string.size(), &wide_string[0], wide_size);
	return wide_string;
}

template <typename T>
inline T gFromString(const char* inString);

template<>
inline float gFromString(const char* inString)
{
	float v;
	std::sscanf(inString, "%f", &v);
	return v;
}

template<>
inline glm::vec2 gFromString(const char* inString)
{
	glm::vec2 v;
	int received = std::sscanf(inString, "%f,%f", &v.x, &v.y);
	if (received != 2)
		received = std::sscanf(inString, "%f %f", &v.x, &v.y);
	return v;
}

template<>
inline glm::vec3 gFromString(const char* inString)
{
	glm::vec3 v;
	int received = std::sscanf(inString, "%f,%f,%f", &v.x, &v.y, &v.z);
	if (received != 3)
		received = std::sscanf(inString, "%f %f %f", &v.x, &v.y, &v.z);
	return v;
}

namespace nameof
{
	template <typename T>
	constexpr std::string_view nameof_enum_type() noexcept
	{
		return nameof::nameof_type<T>().substr(5);
	}
}

// DirectX helpers
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
inline void gSetName(ComPtr<T>& inObject, std::wstring inBaseName, std::wstring inName)
{
	std::wstring new_name = inBaseName + inName;
	inObject->SetName(new_name.c_str());
}

template <typename T>
inline void gSetName(ComPtr<T>& inObject, std::string_view inBaseName, std::string_view inName)
{
	gSetName(inObject, gToWString(inBaseName), gToWString(inName));
}

// System
enum
{
	NUM_FRAMES_IN_FLIGHT = 2,
	NUM_BACK_BUFFERS = 2
};

extern ID3D12Device5*						gDevice;
extern ID3D12DescriptorHeap* 				gRTVDescriptorHeap;
extern ID3D12CommandQueue* 					gCommandQueue;
extern ID3D12GraphicsCommandList4* 			gCommandList;

extern ID3D12Fence* 						gIncrementalFence;			// Fence value increment each frame (most time)
extern HANDLE                       		gIncrementalFenceEvent;		// Allow CPU to wait on fence
extern uint64_t                       		gFenceLastSignaledValue;

extern IDXGISwapChain3* 					gSwapChain;
extern HANDLE                       		gSwapChainWaitableObject;

// Application
struct ShaderTable
{
	ID3D12Resource*							mResource = nullptr;
	glm::uint64								mEntrySize = 0;
	glm::uint32								mRayGenOffset = 0;
	glm::uint32								mRayGenCount = 0;
	glm::uint32								mMissOffset = 0;
	glm::uint32								mMissCount = 0;
	glm::uint32								mHitGroupOffset = 0;
	glm::uint32								mHitGroupCount = 0;
};

template <typename DescriptorIndex>
struct DescriptorHeap
{
	D3D12_DESCRIPTOR_HEAP_TYPE				mType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	int										mCount = 0; // [TODO] Support dynamic range when necessary

	ComPtr<ID3D12DescriptorHeap>			mHeap;
	SIZE_T									mIncrementSize;
	int										mNextDynamicIndex;
	D3D12_CPU_DESCRIPTOR_HANDLE				mCPUHandleStart = { };
	D3D12_GPU_DESCRIPTOR_HANDLE				mGPUHandleStart = { };

	void Reset()
	{
		mHeap = nullptr;
		mIncrementSize = 0;
		mCPUHandleStart = {};
		mGPUHandleStart = {};
	}

	void Initialize()
	{
		gAssert(mType != D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES && mCount != 0);
		gAssert(mCount >= (int)DescriptorIndex::Count);

		Reset();

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = mCount;
		desc.Type = mType;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		gValidate(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mHeap)));

		mIncrementSize = gDevice->GetDescriptorHandleIncrementSize(mType);
		mCPUHandleStart = mHeap->GetCPUDescriptorHandleForHeapStart();
		mGPUHandleStart = mHeap->GetGPUDescriptorHandleForHeapStart();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetHandle(DescriptorIndex inIndex)
	{
		gAssert(inIndex < DescriptorIndex::Count);

		D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
		handle = mCPUHandleStart;
		handle.ptr += static_cast<int>(inIndex) * mIncrementSize;
		return handle;
	}
};

#define MEMBER(parent_type, type, name, default_value) \
	parent_type& name(type in##name) { m##name = in##name; return *this; } \
	type m##name = default_value;

struct Shader
{
#define SHADER_MEMBER(type, name, default_value) MEMBER(Shader, type, name, default_value)

	SHADER_MEMBER(const char*, FileName, nullptr);
	SHADER_MEMBER(const char*, VSName, nullptr);
	SHADER_MEMBER(const char*, PSName, nullptr);
	SHADER_MEMBER(const char*, CSName, nullptr);

	struct DescriptorInfo
	{
		DescriptorInfo(ID3D12Resource* inResource) : mResource(inResource) {}
		DescriptorInfo(ID3D12Resource* inResource, glm::uint inStride) : mResource(inResource), mStride(inStride) {}

		ID3D12Resource*		mResource = nullptr;
		glm::uint			mStride = 0;
	};

	void Reset() { mData = {}; }

	struct Data
	{
		ComPtr<ID3D12RootSignature>					mRootSignature;
		ComPtr<ID3D12PipelineState>					mPipelineState;
		ComPtr<ID3D12StateObject>					mStateObject;
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
	TEXTURE_MEMBER(float, UIScale, 0.0f);
	TEXTURE_MEMBER(const wchar_t*, Path, nullptr);
	TEXTURE_MEMBER(ViewDescriptorIndex, UAVIndex, ViewDescriptorIndex::Count);
	TEXTURE_MEMBER(ViewDescriptorIndex, SRVIndex, ViewDescriptorIndex::Count);
	TEXTURE_MEMBER(DXGI_FORMAT, SRVFormat, DXGI_FORMAT_UNKNOWN);

	Texture& Dimension(glm::uvec3 dimension) 
	{
		mWidth = dimension.x;
		mHeight = dimension.y;
		mDepth = dimension.z;
		return *this;
	}

	int GetPixelSize() const;
	glm::uint64 GetSubresourceSize() const;
	void Initialize();
	void Update();
	void InitializeUpload();

	ComPtr<ID3D12Resource> mResource;
	ComPtr<ID3D12Resource> mUploadResource;

	int mImGuiTextureIndex = -1;

	int mSubresourceCount = 1; // TODO: Support multiple subresources
	bool mLoaded = false;
	std::vector<glm::uint8> mUploadData;
};

// Helper
template <typename RuntimeType>
class RuntimeBase
{
public:
	RuntimeBase() = default;
	~RuntimeBase() = default;

	// Reconstruct this, release all managed resources
	void Reset()
	{
		std::destroy_at<RuntimeType>(static_cast<RuntimeType*>(this));
		std::construct_at<RuntimeType>(static_cast<RuntimeType*>(this));
	}

	// Make the class non-copyable as std::span is used to referencing members, otherwise those should be re-calculated after copy
	RuntimeBase(const RuntimeBase&) = delete;
	RuntimeBase(const RuntimeBase&&) = delete;
	RuntimeBase& operator=(const RuntimeBase&) = delete;
	RuntimeBase& operator=(const RuntimeBase&&) = delete;
};

extern ComPtr<ID3D12Resource>				gConstantGPUBuffer;
extern ComPtr<ID3D12RootSignature>			gDXRGlobalRootSignature;
extern ComPtr<ID3D12StateObject>			gDXRStateObject;

extern ShaderTable							gDXRShaderTable;

struct FrameContext
{
	ComPtr<ID3D12CommandAllocator>			mCommandAllocator;

	ComPtr<ID3D12Resource>					mConstantUploadBuffer;
	void*									mConstantUploadBufferPointer = nullptr;

	glm::uint64								mFenceValue = 0;

	DescriptorHeap<ViewDescriptorIndex>		mViewDescriptorHeap		{ .mType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .mCount = 4096 };
	DescriptorHeap<SamplerDescriptorIndex>	mSamplerDescriptorHeap	{ .mType = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, .mCount = 128 };

	void Reset()
	{
		mCommandAllocator					= nullptr;
		mConstantUploadBuffer				= nullptr;
		mConstantUploadBufferPointer		= nullptr;
		mFenceValue							= 0;
		mViewDescriptorHeap.Reset();
		mSamplerDescriptorHeap.Reset();
	}
};
extern FrameContext							gFrameContexts[];
extern glm::uint32							gFrameIndex;
inline FrameContext&						gGetFrameContext() { return gFrameContexts[gFrameIndex % NUM_FRAMES_IN_FLIGHT]; }

struct Renderer
{
	struct Runtime : RuntimeBase<Runtime>
	{
		Shader								mRayQueryShader				= Shader().FileName("Shader/Raytracing.hlsl").CSName("RayQueryCS");
		Shader								mDiffTexture2DShader		= Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture2DShader");
		Shader								mDiffTexture3DShader		= Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture3DShader");
		Shader								mCompositeShader			= Shader().FileName("Shader/Composite.hlsl").VSName("ScreenspaceTriangleVS").PSName("CompositePS");

		Shader								mSentinelShader				= Shader();
		std::span<Shader>					mShaders					= std::span<Shader>(&mRayQueryShader, &mSentinelShader);

		Texture								mScreenColorTexture			= Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenColorUAV).Name("Renderer.ScreenColorTexture");
		Texture								mScreenDebugTexture			= Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenDebugUAV).Name("Renderer.ScreenDebugTexture");

		Texture								mSentinelTexture;
		std::span<Texture>					mTextures					= std::span<Texture>(&mScreenColorTexture, &mSentinelTexture);

		ComPtr<ID3D12Resource>				mBackBuffers[NUM_BACK_BUFFERS] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE			mBufferBufferRTVs[NUM_BACK_BUFFERS] = {};
	};
	Runtime mRuntime;

	void									Initialize();
	void									Finalize();
	void									ImGuiShowTextures();
	void									ResetScreen();
	void									AcquireBackBuffers();
	void									ReleaseBackBuffers();

	void									Setup(const Shader& inShader)
	{
		Setup(inShader.mData, inShader.mCSName == nullptr);
	}

	void									Setup(const Shader::Data& inShaderData, bool inGraphics = false)
	{
		// Heaps of Dynamic Resources needs to be set before RootSignature
		// See https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html#setdescriptorheaps-and-setrootsignature
		ID3D12DescriptorHeap* bindless_heaps[] =
		{
			gGetFrameContext().mViewDescriptorHeap.mHeap.Get(),
			gGetFrameContext().mSamplerDescriptorHeap.mHeap.Get(),
		};
		gCommandList->SetDescriptorHeaps(ARRAYSIZE(bindless_heaps), bindless_heaps);

		if (inGraphics)
			gCommandList->SetGraphicsRootSignature(inShaderData.mRootSignature.Get());
		else
			gCommandList->SetComputeRootSignature(inShaderData.mRootSignature.Get());

		if (inShaderData.mStateObject != nullptr)
			gCommandList->SetPipelineState1(inShaderData.mStateObject.Get());
		else
			gCommandList->SetPipelineState(inShaderData.mPipelineState.Get());

		// Root parameters need to be set after RootSignature
		if (inGraphics)
			gCommandList->SetGraphicsRootConstantBufferView((int)RootParameterIndex::Constants, gConstantGPUBuffer->GetGPUVirtualAddress());
		else
			gCommandList->SetComputeRootConstantBufferView((int)RootParameterIndex::Constants, gConstantGPUBuffer->GetGPUVirtualAddress());
	}

	bool									mReloadShader							= false;
	bool									mDumpDisassemblyRayQuery				= false;
	bool									mPrintStateObjectDesc					= false;
																					
	bool									mUseRayQuery							= true;	
};
extern Renderer								gRenderer;

extern Texture*								gDumpTexture;
extern Texture								gDumpTextureProxy;

extern Constants							gConstants;

// String literals
static const wchar_t*						kDefaultRayGenerationShader	= L"DefaultRayGeneration";
static const wchar_t*						kDefaultMissShader			= L"DefaultMiss";
static const wchar_t*						kDefaultClosestHitShader	= L"DefaultClosestHit";
static const wchar_t*						kDefaultHitGroup			= L"DefaultHitGroup";

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

struct BarrierScope
{
	BarrierScope(ID3D12GraphicsCommandList4* command_list, ID3D12Resource* resource, D3D12_RESOURCE_STATES state_before, D3D12_RESOURCE_STATES state_after)
		: mCommandList(command_list), mResource(resource), mStateBefore(state_before), mStateAfter(state_after)
	{
		gBarrierTransition(mCommandList, mResource, mStateBefore, mStateAfter);
	}

	~BarrierScope()
	{
		gBarrierTransition(mCommandList, mResource, mStateAfter, mStateBefore);
	}

private:
	ID3D12GraphicsCommandList4* mCommandList{};
	ID3D12Resource* mResource{};
	D3D12_RESOURCE_STATES mStateBefore{};
	D3D12_RESOURCE_STATES mStateAfter{};
};

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

inline D3D12_RESOURCE_DESC gGetUAVResourceDesc(UINT64 inWidth)
{
	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(inWidth);
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	return desc;
}

namespace ImGui
{
	void Textures(std::span<Texture> inTextures, const std::string& inName = "Texture", ImGuiTreeNodeFlags inFlags = 0);
}