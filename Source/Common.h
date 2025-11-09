#pragma once

#define WIN32_LEAN_AND_MEAN

#include <d3d12.h>			// For D3D12
#include <dxgi1_4.h>		// For DXGI
#include <dxcapi.h>			// For IDxcCompiler
#include <shellapi.h>		// For ShellExecuteA
#include <dxgidebug.h>		// For IDXGIDebug1
#include <d3d12shader.h>	// For shader reflection
#include <wrl.h>			// For ComPtr. See https://github.com/Microsoft/DirectXTK/wiki/ComPtr
using Microsoft::WRL::ComPtr;
#include <pix3.h>			// For PIXScopedEvent

#include <string>
#include <fstream>
#include <array>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>
#include <span>
#include <chrono>
#include <set>

#include "Thirdparty/glm.h"
#include "Thirdparty/nameof/include/nameof.hpp"
#include "Thirdparty/DirectXTex/DirectXTex/DirectXTex.h"
#include "Thirdparty/DirectXTex/DirectXTex/d3dx12.h"
#include "Thirdparty/nvapi/nvapi.h"
#include "ImGui/imgui_impl_helper.h"
#include "ImGui/imgui_impl_dx12.h"
#include "Thirdparty/implot/implot.h"

#include "../Shader/Shared.h"

// Common helpers
#define gAssert assert
#define gVerify(condition)				\
	do {								\
		if (!(condition))				\
		{								\
			*((volatile int *)0) = 0;	\
		}								\
	} while(0)
#define UNUSED(_VAR) ((void)(_VAR))

template <typename T>
constexpr inline T gAlignUpDiv(T value, T alignment)
{
	return (value + alignment - 1) / alignment;
}

template <typename T>
constexpr inline T gAlignUp(T value, T alignment)
{
	return gAlignUpDiv(value, alignment) * alignment;
}

template <typename T>
constexpr inline T gMin(T lhs, T rhs)
{
	return lhs < rhs ? lhs : rhs;
}

template <typename T>
constexpr inline T gMax(T lhs, T rhs)
{
	return lhs > rhs ? lhs : rhs;
}

constexpr inline float gMinComponent(float2 v) { return std::min(v.x, v.y); }
constexpr inline float gMinComponent(float3 v) { return std::min(std::min(v.x, v.y), v.z); }
constexpr inline float gMinComponent(float4 v) { return std::min(std::min(std::min(v.x, v.y), v.z), v.w); }
constexpr inline float gMaxComponent(float2 v) { return std::max(v.x, v.y); }
constexpr inline float gMaxComponent(float3 v) { return std::max(std::max(v.x, v.y), v.z); }
constexpr inline float gMaxComponent(float4 v) { return std::max(std::max(std::max(v.x, v.y), v.z), v.w); }

template <typename T, size_t N>
struct ArraySizeHelper {
	using type = typename std::conditional<
		(N <= std::numeric_limits<uint32_t>::max()),
		uint32_t,
		uint64_t
	>::type;
	static constexpr type value = static_cast<type>(N);
};

template<typename T, size_t N>
constexpr typename ArraySizeHelper<T, N>::type gArraySize(const T (&)[N])
{
	return ArraySizeHelper<T, N>::value;
}

inline std::string gToLower(const std::string_view& inStringView)
{
	std::string result(inStringView);
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

namespace glm
{
	template<length_t L, typename T, qualifier Q = defaultp>
	GLM_FUNC_QUALIFIER vec<L, T, Q> lerp(vec<L, T, Q> const& x, vec<L, T, Q> const& y, T a)
	{
		GLM_STATIC_ASSERT(std::numeric_limits<T>::is_iec559, "'lerp' only accept floating-point inputs");

		// Lerp is only defined in [0, 1]
		assert(a >= static_cast<T>(0));
		assert(a <= static_cast<T>(1));

		return x * (static_cast<T>(1) - a) + (y * a);
	}
}

template<typename T>
std::tuple<T, T, float> gMakeLerpTuple(const std::vector<T>& inArray, float inRatio)
{
	int size									= static_cast<int>(inArray.size());
	float float_index							= inRatio * size;
	int int_index								= static_cast<int>(std::floor(float_index));
	const T& from								= inArray[std::clamp(int_index, 0, size - 1)];
	const T& to									= inArray[std::clamp(int_index + 1, 0, size - 1)];
	float fraction								= std::clamp(float_index - int_index, 0.0f, 1.0f);
	return std::make_tuple(from, to, fraction);
};

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

inline std::filesystem::path gCreateDumpFolder()
{
	std::filesystem::path directory = ".\\Dump\\";
	std::filesystem::create_directory(directory);

	return directory;
}

inline void gOpenDumpFolder()
{
	std::filesystem::path dump_folder = gCreateDumpFolder();

	std::filesystem::path command = "";
	command += std::filesystem::current_path();
	command += "\\";
	command += dump_folder;
	ShellExecuteA(nullptr, "open", command.string().c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
}

inline void gOpenSceneFolder(const std::string_view inPath)
{
	std::filesystem::path command = "";
	"";
	command += std::filesystem::current_path();
	command += "\\";
	command += inPath;
	command = command.parent_path();
	ShellExecuteA(nullptr, "open", command.string().c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
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

template <typename T>
inline void gFromString(const char* inString, T& outValue) { if (inString == nullptr) return; outValue = gFromString<T>(inString); }

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

template <>
inline glm::mat4x4 gFromString(const char* inString) 
{ 
	glm::mat4x4 matrix = glm::mat4x4(1.0f); 
	std::sscanf(inString, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", 
		&matrix[0][0], &matrix[1][0], &matrix[2][0], &matrix[3][0], 
		&matrix[0][1], &matrix[1][1], &matrix[2][1], &matrix[3][1], 
		&matrix[0][2], &matrix[1][2], &matrix[2][2], &matrix[3][2],
		&matrix[0][3], &matrix[1][3], &matrix[2][3], &matrix[3][3]);
	return matrix;
}

template <typename T>
inline std::string gToString(const T& inValue) { return std::to_string(inValue); }


template <>
inline std::string gToString(const LPCSTR& inValue) { return std::string(inValue); }

template <>
inline std::string gToString(const glm::mat4x4& inValue) 
{ 
	return std::format("{} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}", 
		inValue[0][0], inValue[1][0], inValue[2][0], inValue[3][0], 
		inValue[0][1], inValue[1][1], inValue[2][1], inValue[3][1], 
		inValue[0][2], inValue[1][2], inValue[2][2], inValue[3][2], 
		inValue[0][3], inValue[1][3], inValue[2][3], inValue[3][3]);
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
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, hr, 0, message, (DWORD)std::size(message), nullptr);
		OutputDebugStringA(message);
		assert(false);
	}
}

template <typename T>
inline void gSetName(ComPtr<T>& inObject, std::wstring inPrefix, std::wstring inName, std::wstring inSuffix)
{
	std::wstring new_name = inPrefix + inName + inSuffix;
	inObject->SetName(new_name.c_str());
}

template <typename T>
inline void gSetName(ComPtr<T>& inObject, std::string_view inPrefix, std::string_view inName, std::string_view inSuffix)
{
	gSetName(inObject, gToWString(inPrefix), gToWString(inName), gToWString(inSuffix));
}

constexpr int								kScreenWidth = 1920;
constexpr int								kScreenHeight = 1080;

constexpr int								kVertexCountPerTriangle = 3;

extern bool									gHeadless;
extern bool									gHeadlessDone;

extern ID3D12Device7*						gDevice;
extern ID3D12DescriptorHeap* 				gRTVDescriptorHeap;
extern ID3D12CommandQueue* 					gCommandQueue;
extern ID3D12GraphicsCommandList4* 			gCommandList;

extern ID3D12QueryHeap*						gQueryHeap;
struct Stats
{
	struct InstructionCount
	{
		int									mRayQuery = 0;	
	};
	InstructionCount						mInstructionCount;

	// [NOTE] Time might look longer than necessary if GPU is not full load, turn off Vsync to force it run full speed
	struct TimeMS
	{
		float								mUpload = 0;
		float								mRenderer = 0;
		float								mScene = 0;
		float								mAtmosphere = 0;
		float								mCloud = 0;
		float								mTextureGenerator = 0;
		float								mBRDFSlice = 0;
		float								mClear = 0;
		float								mDepths = 0;
		float								mPrepareLights = 0;
		float								mRayQuery = 0;
		float								mComposite = 0;
	};
	TimeMS									mTimeMS;
};
extern Stats								gStats;

struct Configs
{
	bool									mShaderDebug = true;
	bool									mUseHalf = true;
	bool									mUseTexture = true;

	bool									mTestHitShader = false;

	bool									mNanoVDBGenerateTexture = false;
	bool									mNanoVDBUseTexture = false;

	std::set<BSDF>							mSceneBSDFs;
};
extern Configs								gConfigs;

extern ID3D12Fence* 						gIncrementalFence;			// Fence value increment each frame (most time)
extern HANDLE                       		gIncrementalFenceEvent;		// Allow CPU to wait on fence
extern UINT64                       		gFenceLastSignaledValue;

extern IDXGISwapChain3* 					gSwapChain;
extern HANDLE                       		gSwapChainWaitableObject;

struct NVAPI
{
	bool									mInitialized = false;
	bool									mMicromapSupported = false;
	bool									mClustersSupported = false;
	bool									mLinearSweptSpheresSupported = false;
	bool									mSpheresSupported = false;
	bool									mShaderExecutionReorderingSupported = false;
	bool									mFakeUAVEnabled = false;

	NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE  mEndcapMode = NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED;

	bool									mLSSWireframeEnabled = false;
	NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE  mLSSWireframeEndcapMode = NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED;
	float									mLSSWireframeRadius = 0.01f;
};
extern NVAPI								gNVAPI;

template <typename DescriptorIndex>
struct DescriptorHeap
{
	D3D12_DESCRIPTOR_HEAP_TYPE				mType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	int										mCount = 0; // [TODO] Support dynamic range when necessary
	bool									mForceCPU = false;

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

		Reset();

		bool shader_visible = !mForceCPU && mType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || mType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = mCount;
		desc.Type = mType;
		desc.Flags = shader_visible ?  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		gValidate(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mHeap)));

		mIncrementSize = gDevice->GetDescriptorHandleIncrementSize(mType);
		mCPUHandleStart = mHeap->GetCPUDescriptorHandleForHeapStart();
		mGPUHandleStart = shader_visible ? mHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE { 0 };

		gAssert((mIncrementSize & ImGui_ImplDX12_ImTextureID_Mask_3D) == 0);
		gAssert((mGPUHandleStart.ptr & ImGui_ImplDX12_ImTextureID_Mask_3D) == 0);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(DescriptorIndex inIndex)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
		handle = mCPUHandleStart;
		handle.ptr += static_cast<int>(inIndex) * mIncrementSize;
		return handle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(DescriptorIndex inIndex)
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle = {};
		handle = mGPUHandleStart;
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

	SHADER_MEMBER(const wchar_t*, RayGenerationName, nullptr);
	SHADER_MEMBER(const wchar_t*, MissName, nullptr);
	SHADER_MEMBER(const wchar_t*, AnyHitName, nullptr);
	SHADER_MEMBER(const Shader*, AnyHitReference, nullptr);
	SHADER_MEMBER(const wchar_t*, ClosestHitName, nullptr);
	SHADER_MEMBER(const wchar_t*, IntersectionName, nullptr);
	SHADER_MEMBER(const Shader*, RootSignatureReference, nullptr);
	SHADER_MEMBER(D3D12_PRIMITIVE_TOPOLOGY_TYPE, Topology, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	SHADER_MEMBER(bool, DepthWrite, false);
	SHADER_MEMBER(D3D12_COMPARISON_FUNC, DepthFunc, D3D12_COMPARISON_FUNC_ALWAYS);
	SHADER_MEMBER(DXGI_FORMAT, RTVFormat, DXGI_FORMAT_UNKNOWN);
	SHADER_MEMBER(DXGI_FORMAT, DSVFormat, DXGI_FORMAT_UNKNOWN);
	const wchar_t* HitName() const { return mAnyHitName != nullptr ? mAnyHitName : (mClosestHitName != nullptr ? mClosestHitName : mIntersectionName); }
	const std::wstring HitGroupName() const { if (HitName() == nullptr) return L""; std::wstring name = HitName(); name += L"Group"; return name; }

	struct DescriptorInfo
	{
		DescriptorInfo(ID3D12Resource* inResource) : mResource(inResource) {}
		DescriptorInfo(ID3D12Resource* inResource, uint32_t inStride) : mResource(inResource), mStride(inStride) {}

		ID3D12Resource*		mResource = nullptr;
		uint32_t			mStride = 0;
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

struct Texture;

struct Buffer
{
#define BUFFER_MEMBER(type, name, default_value) MEMBER(Buffer, type, name, default_value)

	BUFFER_MEMBER(uint,						ByteCount,		0);
	BUFFER_MEMBER(std::string,				Name,			"");
	BUFFER_MEMBER(ViewDescriptorIndex,		CBVIndex,		ViewDescriptorIndex::Invalid);
	BUFFER_MEMBER(ViewDescriptorIndex,		SRVIndex,		ViewDescriptorIndex::Invalid);
	BUFFER_MEMBER(ViewDescriptorIndex,		UAVIndex,		ViewDescriptorIndex::Invalid);
	BUFFER_MEMBER(uint,						Stride,			0);
	BUFFER_MEMBER(bool,						GPU,			true);
	BUFFER_MEMBER(bool,						Upload,			false);
	BUFFER_MEMBER(bool,						UploadOnce,		false);
	BUFFER_MEMBER(bool,						Readback,		false);
	
	void Initialize();
	void Update();
	template <typename T>
	T* ReadbackAs(uint inFrameContextIndex) { return static_cast<T*>(mReadbackPointer[inFrameContextIndex]); }

	ComPtr<ID3D12Resource>					mResource;
	ComPtr<ID3D12Resource>					mUploadResource[kFrameInFlightCount];
	void*									mUploadPointer[kFrameInFlightCount] = { nullptr };
	ComPtr<ID3D12Resource>					mReadbackResource[kFrameInFlightCount];
	void*									mReadbackPointer[kFrameInFlightCount] = { nullptr };

	bool									mLoaded = false;
};

struct Texture
{
#define TEXTURE_MEMBER(type, name, default_value) MEMBER(Texture, type, name, default_value)

	TEXTURE_MEMBER(uint32_t,				Width,			1);
	TEXTURE_MEMBER(uint32_t,				Height,			1);
	TEXTURE_MEMBER(uint32_t,				Depth,			1);
	TEXTURE_MEMBER(DXGI_FORMAT,				Format,			DXGI_FORMAT_R32G32B32A32_FLOAT);
	TEXTURE_MEMBER(std::string,				Name,			"");
	TEXTURE_MEMBER(float,					UIScale,		0.0f);
	TEXTURE_MEMBER(std::filesystem::path,	Path,			L"");
	TEXTURE_MEMBER(ViewDescriptorIndex,		UAVIndex,		ViewDescriptorIndex::Invalid);
	TEXTURE_MEMBER(ViewDescriptorIndex,		SRVIndex,		ViewDescriptorIndex::Invalid);
	TEXTURE_MEMBER(DXGI_FORMAT,				SRVFormat,		DXGI_FORMAT_UNKNOWN);
	TEXTURE_MEMBER(RTVDescriptorIndex,		RTVIndex,		RTVDescriptorIndex::Invalid);
	TEXTURE_MEMBER(DSVDescriptorIndex,		DSVIndex,		DSVDescriptorIndex::Invalid);

	Texture& Dimension(glm::uvec3 dimension) 
	{
		mWidth = dimension.x;
		mHeight = dimension.y;
		mDepth = dimension.z;
		return *this;
	}

	int GetPixelSize() const;
	uint64_t GetSubresourceSize() const;
	void Initialize();
	void Update();
	void InitializeUpload();
	void Readback();

	ComPtr<ID3D12Resource> mResource;
	ComPtr<ID3D12Resource> mUploadResource;

	int mSubresourceCount = 1; // TODO: Support multiple subresources
	bool mLoaded = false;
	std::vector<uint8_t> mUploadData;
	float* mEXRData = nullptr;
};

struct ShaderTable
{
	ComPtr<ID3D12Resource>	mResource					= nullptr;
	uint64_t				mEntrySize					= 0;
	uint64_t				mRayGenOffset				= 0;
	uint64_t				mRayGenCount				= 0;
	uint64_t				mMissOffset					= 0;
	uint64_t				mMissCount					= 0;
	uint64_t				mHitGroupOffset				= 0;
	uint64_t				mHitGroupCount				= 0;
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

struct FrameContext
{
	ComPtr<ID3D12CommandAllocator>			mCommandAllocator;

	uint64_t								mFenceValue = 0;

	DescriptorHeap<ViewDescriptorIndex>		mViewDescriptorHeap			{ .mType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .mCount = 1000000 };
	DescriptorHeap<SamplerDescriptorIndex>	mSamplerDescriptorHeap		{ .mType = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, .mCount = 2048 };

	// For ClearUnorderedAccessViewFloat, what is the best practice for this? Create on the fly?
	DescriptorHeap<ViewDescriptorIndex>		mClearDescriptorHeap		{ .mType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .mCount = 4096, .mForceCPU = true };

	void Reset()
	{
		mCommandAllocator					= nullptr;
		mFenceValue							= 0;
		mViewDescriptorHeap.Reset();
		mSamplerDescriptorHeap.Reset();
		mClearDescriptorHeap.Reset();
	}
};
extern FrameContext							gFrameContexts[];
extern uint32_t								gFrameIndex;
inline uint32_t								gGetFrameContextIndex() { return gFrameIndex % kFrameInFlightCount; }
inline FrameContext&						gGetFrameContext() { return gFrameContexts[gGetFrameContextIndex()]; }

struct CPUContext
{
	DescriptorHeap<RTVDescriptorIndex>		mRTVDescriptorHeap{ .mType = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .mCount = 128 };
	DescriptorHeap<DSVDescriptorIndex>		mDSVDescriptorHeap{ .mType = D3D12_DESCRIPTOR_HEAP_TYPE_DSV, .mCount = 128 };

	Texture*								mDumpTextureRef = nullptr;
	Texture									mDumpTextureProxy = {};

	void Reset()
	{
		mRTVDescriptorHeap.Reset();
		mDSVDescriptorHeap.Reset();

		mDumpTextureRef = nullptr;
		mDumpTextureProxy = {};
	}
};
extern CPUContext							gCPUContext;
extern Constants							gConstants;

inline void gBarrierTransition(ID3D12GraphicsCommandList4* inCommandList, ID3D12Resource* inResource, D3D12_RESOURCE_STATES inBeforeState, D3D12_RESOURCE_STATES inAfterState)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = inResource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = inBeforeState;
	barrier.Transition.StateAfter = inAfterState;
	inCommandList->ResourceBarrier(1, &barrier);
}

struct BarrierScope
{
	BarrierScope(ID3D12GraphicsCommandList4* inCommandList, ID3D12Resource* inResource, D3D12_RESOURCE_STATES inCurrentState, D3D12_RESOURCE_STATES inScopeState)
		: mCommandList(inCommandList), mResource(inResource), mCurrentState(inCurrentState), mScopeState(inScopeState)
	{
		gBarrierTransition(mCommandList, mResource, mCurrentState, mScopeState);
	}

	~BarrierScope()
	{
		gBarrierTransition(mCommandList, mResource, mScopeState, mCurrentState);
	}

private:
	ID3D12GraphicsCommandList4* mCommandList{};
	ID3D12Resource* mResource{};
	D3D12_RESOURCE_STATES mCurrentState{};
	D3D12_RESOURCE_STATES mScopeState{};
};

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

inline D3D12_HEAP_PROPERTIES gGetReadbackHeapProperties()
{
	D3D12_HEAP_PROPERTIES props = {};
	props.Type = D3D12_HEAP_TYPE_READBACK;
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

inline D3D12_RESOURCE_DESC gGetUAVResourceDesc(UINT64 inWidth)
{
	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(inWidth);
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	return desc;
}

// From https://github.com/microsoft/DirectX-Graphics-Samples/blob/e5ea2ac7430ce39e6f6d619fd85ae32581931589/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingHelloWorld/DirectXRaytracingHelper.h#L172
static void gPrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
{
	std::wstringstream wstr;
	wstr << L"\n";
	wstr << L"--------------------------------------------------------------------\n";
	wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
	if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
	if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

	auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
	{
		std::wostringstream woss;
		for (UINT i = 0; i < numExports; i++)
		{
			woss << L"|";
			if (depth > 0)
			{
				for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
			}
			woss << L" [" << i << L"]: ";
			if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
			woss << exports[i].Name << L"\n";
		}
		return woss.str();
	};

	for (UINT i = 0; i < desc->NumSubobjects; i++)
	{
		wstr << L"| [" << i << L"]: ";
		switch (desc->pSubobjects[i].Type)
		{
		case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
			wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
			wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
			wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
			break;
		case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
		{
			wstr << L"DXIL Library 0x";
			auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
			wstr << ExportTree(1, lib->NumExports, lib->pExports);
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
		{
			wstr << L"Existing Library 0x";
			auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << collection->pExistingCollection << L"\n";
			wstr << ExportTree(1, collection->NumExports, collection->pExports);
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
		{
			wstr << L"Subobject to Exports Association (Subobject [";
			auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
			UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
			wstr << index << L"])\n";
			for (UINT j = 0; j < association->NumExports; j++)
			{
				wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
			}
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
		{
			wstr << L"DXIL Subobjects to Exports Association (";
			auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
			wstr << association->SubobjectToAssociate << L")\n";
			for (UINT j = 0; j < association->NumExports; j++)
			{
				wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
			}
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
		{
			wstr << L"Raytracing Shader Config\n";
			auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
			wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
			wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
		{
			wstr << L"Raytracing Pipeline Config\n";
			auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
			wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
			break;
		}
		case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
		{
			wstr << L"Hit Group (";
			auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
			wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
			wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
			wstr << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
			wstr << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
			break;
		}
		}
		wstr << L"|--------------------------------------------------------------------\n";
	}
	wstr << L"\n";
	OutputDebugStringW(wstr.str().c_str());
}

namespace ImGui
{
	extern float gDpiScale;

	void Texture1(Texture& inTexture);
	void Textures(std::span<Texture> inTextures, const std::string& inName = "Texture", ImGuiTreeNodeFlags inFlags = 0);
}

namespace
{
	using std::chrono::high_resolution_clock;
	using std::chrono::duration_cast;
	using std::chrono::duration;
	using std::chrono::milliseconds;
}

struct CPUTimeScope
{
	CPUTimeScope(std::string_view inText)
	{
		mText = inText;
		mStart = std::chrono::high_resolution_clock::now();
	}

	~CPUTimeScope()
	{
		mEnd = std::chrono::high_resolution_clock::now();

		std::chrono::duration<float, std::milli> ms = mEnd - mStart;
		std::string message = std::format("{} costs {:.2f} ms\n", mText, ms.count());
		gTrace(message);
	}

	std::chrono::high_resolution_clock::time_point mStart;
	std::chrono::high_resolution_clock::time_point mEnd;
	std::string_view mText;
};