#pragma once

#include "Common.h"

static constexpr DXGI_FORMAT					kBackBufferFormat			= DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr uint							kTimestampCount				= 1024;

struct Renderer
{
	struct Runtime : RuntimeBase<Runtime>
	{
		Shader									mRayQueryShader				= Shader().FileName("Shader/RayQuery.hpp").CSName("RayQueryCS");
		Shader									mDepthShader				= Shader().FileName("Shader/RayQuery.hpp").VSName("ScreenspaceTriangleVS").PSName("DepthPS").DepthWrite(true).DSVFormat(DXGI_FORMAT_D32_FLOAT);
		Shader									mPrepareLightsShader		= Shader().FileName("Shader/PrepareLights.hpp").CSName("PrepareLightsCS");
		Shader									mClearShader				= Shader().FileName("Shader/Composite.hpp").CSName("ClearCS");
		Shader									mClearBufferShader			= Shader().FileName("Shader/Composite.hpp").CSName("ClearBufferCS");
		Shader									mGenerateTextureShader		= Shader().FileName("Shader/Composite.hpp").CSName("GeneratTextureCS");
		Shader									mBRDFSliceShader			= Shader().FileName("Shader/Composite.hpp").CSName("BRDFSliceCS");
		Shader									mReadbackShader				= Shader().FileName("Shader/Composite.hpp").CSName("ReadbackCS");
		Shader									mNanoVDBVisualizeShader		= Shader().FileName("Shader/Composite.hpp").CSName("NanoVDBVisualizeCS");
		Shader									mDiffTexture2DShader		= Shader().FileName("Shader/DiffTexture.hpp").CSName("DiffTexture2DShader");
		Shader									mDiffTexture3DShader		= Shader().FileName("Shader/DiffTexture.hpp").CSName("DiffTexture3DShader");
		Shader									mLineShader					= Shader().FileName("Shader/Composite.hpp").VSName("LineVS").PSName("LinePS").Topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE).DepthFunc(D3D12_COMPARISON_FUNC_LESS).RTVFormat(kBackBufferFormat).DSVFormat(DXGI_FORMAT_D32_FLOAT);
		Shader									mLineHiddenShader			= Shader().FileName("Shader/Composite.hpp").VSName("LineVS").PSName("LineHiddenPS").Topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE).DepthFunc(D3D12_COMPARISON_FUNC_GREATER).RTVFormat(kBackBufferFormat).DSVFormat(DXGI_FORMAT_D32_FLOAT);
		Shader									mCompositeShader			= Shader().FileName("Shader/Composite.hpp").VSName("ScreenspaceTriangleVS").PSName("CompositePS").RTVFormat(kBackBufferFormat).DSVFormat(DXGI_FORMAT_D32_FLOAT);
		Shader									mSlangShader				= Shader().FileName("Shader/Slang.slang").CSName("SlangCS");
		Shader									mSentinelShader				= Shader();
		std::span<Shader>						mShaders					= std::span<Shader>(&mRayQueryShader, &mSentinelShader);

		Shader									mRayGenerationShader		= Shader().FileName("Shader/RayGeneration.hpp").RayGenerationName(L"RayGeneration");
		Shader									mMissShader					= Shader().FileName("Shader/Miss.hpp").MissName(L"Miss");
		Shader									mAnyHitShader				= Shader().FileName("Shader/AnyHit.hpp").AnyHitName(L"AnyHit");														// AnyHit MUST comes before HitGroup referencing it when being AddToStateObject, otherwise DXGI_ERROR_DRIVER_INTERNAL_ERROR
		Shader									mClosestHit100Shader		= Shader().FileName("Shader/ClosestHit100.hpp").ClosestHitName(L"ClosestHit100");									// ClosestHit without AnyHit
		Shader									mClosestHit010Shader		= Shader().FileName("Shader/ClosestHit010.hpp").ClosestHitName(L"ClosestHit010").AnyHitReference(&mAnyHitShader);	// ClosestHit with AnyHit in different library
		Shader									mClosestHit001Shader		= Shader().FileName("Shader/ClosestHit001.hpp").ClosestHitName(L"ClosestHit001").AnyHitName(L"AnyHit001");			// ClosestHit with AnyHit in same library
		Shader									mCollectionSentinelShader	= Shader();
		std::span<Shader>						mCollectionShaders			= std::span<Shader>(&mMissShader,			&mCollectionSentinelShader);
		std::span<Shader>						mHitGroupShaders			= std::span<Shader>(&mClosestHit100Shader,	&mCollectionSentinelShader);
		Shader									mLibShader					= Shader();
		ShaderTable								mLibShaderTable;

		Texture									mScreenColorTexture			= Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenColorUAV).SRVIndex(ViewDescriptorIndex::ScreenColorSRV).Name("Renderer.ScreenColorTexture");
		Texture									mScreenDebugTexture			= Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenDebugUAV).SRVIndex(ViewDescriptorIndex::ScreenDebugSRV).Name("Renderer.ScreenDebugTexture");
		Texture									mScreenReadbackTexture		= Texture().Format(DXGI_FORMAT_R8G8B8A8_UNORM).UAVIndex(ViewDescriptorIndex::ScreenReadbackUAV).SRVIndex(ViewDescriptorIndex::ScreenReadbackSRV).Name("Renderer.ScreenReadbackTexture");
		Texture									mScreenDepthTexture			= Texture().Format(DXGI_FORMAT_D32_FLOAT).DSVIndex(DSVDescriptorIndex::ScreenDepth).SRVIndex(ViewDescriptorIndex::ScreenDepthSRV).SRVFormat(DXGI_FORMAT_R32_FLOAT).Name("Renderer.ScreenDepthTexture");
		Texture									mScreenReservoirTexture		= Texture().Format(DXGI_FORMAT_R32G32B32A32_UINT).UAVIndex(ViewDescriptorIndex::ScreenReservoirUAV).SRVIndex(ViewDescriptorIndex::ScreenReservoirSRV).Name("Renderer.ScreenReservoirTexture");

		Texture									mScreenSentinelTexture;
		std::span<Texture>						mScreenTextures				= std::span<Texture>(&mScreenColorTexture, &mScreenSentinelTexture);

		Texture									mBRDFSliceTexture			= Texture().Width(512).Height(512).Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::BRDFSliceUAV).SRVIndex(ViewDescriptorIndex::BRDFSliceSRV).Name("Renderer.BRDFSlice");
		// Texture									mShapeNoise3DTexture		= Texture().Width(128).Height(128).Depth(128).Format(DXGI_FORMAT_R8_UNORM).SRVIndex(ViewDescriptorIndex::ShapeNoise3DSRV).Name("Renderer.ShapeNoise3D").Path(L"Asset/TileableVolumeNoise/noiseShapePacked.dds");
		Texture									mErosionNoise3DTexture		= Texture().Width(32).Height(32).Depth(32).Format(DXGI_FORMAT_R8_UNORM).SRVIndex(ViewDescriptorIndex::ErosionNoise3DSRV).Name("Renderer.ErosionNoise3D").Path(L"Asset/TileableVolumeNoise/noiseErosionPacked.dds");
		// Texture									mUVCheckerTexture			= Texture().Width(1024).Height(1024).Format(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).SRVIndex(ViewDescriptorIndex::UVCheckerSRV).Name("Renderer.UVCheckerMap").Path(L"Asset/UVChecker-map/UVCheckerMaps/UVCheckerMap01-1024.png");
		// Texture									mIESTexture					= Texture().Width(256).Height(16).Format(DXGI_FORMAT_R32_FLOAT).SRVIndex(ViewDescriptorIndex::IESSRV).Name("Renderer.IES").Path(L"Asset/IES/007cfb11e343e2f42e3b476be4ab684e/IES.hdr");
		Texture									mGenerateTexture			= Texture().Width(2 * 20).Height(2 * 80).Format(DXGI_FORMAT_R8G8B8A8_UNORM).UAVIndex(ViewDescriptorIndex::GeneratedUAV).SRVIndex(ViewDescriptorIndex::GeneratedSRV).Name("Renderer.Generated");

		Texture									mSentinelTexture;
		std::span<Texture>						mTextures					= std::span<Texture>(&mBRDFSliceTexture, &mSentinelTexture);

		Texture									mBackBuffers[kFrameInFlightCount] = { 
																			Texture().Format(kBackBufferFormat).RTVIndex(RTVDescriptorIndex::BackBuffer0).Name("Renderer.BackBuffer0"),
																			Texture().Format(kBackBufferFormat).RTVIndex(RTVDescriptorIndex::BackBuffer1).Name("Renderer.BackBuffer1") };

		Buffer									mConstantsBuffer			= Buffer().Stride(sizeof(Constants)).CBVIndex(ViewDescriptorIndex::ConstantsCBV).Name("Constants").Upload(true);
		Buffer									mPixelInspectionBuffer		= Buffer().Stride(sizeof(PixelInspection)).UAVIndex(ViewDescriptorIndex::PixelInspectionUAV).Name("PixelInspection").Readback(true);
		Buffer									mRayInspectionBuffer		= Buffer().Stride(sizeof(RayInspection)).UAVIndex(ViewDescriptorIndex::RayInspectionUAV).Name("RayInspection");
		Buffer									mQueryBuffer				= Buffer().Stride(sizeof(UINT64)).ElementCount(kTimestampCount).Name("Query").GPU(false).Readback(true);
		Buffer 									mSpatialHashBuffer			= Buffer().Stride(sizeof(uint32_t)).ElementCount(kSpatialHashSize).UAVIndex(ViewDescriptorIndex::SpatialHashUAV).Name("SpatialHash");
		Buffer 									mSpatialDataBuffer			= Buffer().Stride(sizeof(uint32_t)).ElementCount(kSpatialHashSize).UAVIndex(ViewDescriptorIndex::SpatialDataUAV).Name("SpatialData");
		Buffer 									mShaderPrintBuffer			= Buffer().Stride(sizeof(uint32_t)).ElementCount(64 * 1024).UAVIndex(ViewDescriptorIndex::ShaderPrintUAV).Readback(true).Name("ShaderPrintUAV");
		Buffer									mSentinelBuffer;
		std::span<Buffer>						mBuffers					= std::span<Buffer>(&mConstantsBuffer, &mSentinelBuffer);
	};
	Runtime mRuntime;

	struct Compiler
	{
		void									Initialize();
		void									Finalize();

		void									CreateCommonRootSignature();
		void									CreateLocalRootSignature();
		ComPtr<ID3D12RootSignature>				CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc);

		ComPtr<ID3D12RootSignature>				mCommonRootSignature;
		ComPtr<ID3D12RootSignature>				mLocalRootSignature;

		bool									CreateVSPSPipelineState(const char* inFileName, const char* inVSName, const char* inPSName, Shader& ioShader);
		bool									CreateCSPipelineState(const char* inFileName, const char* inCSName, Shader& ioShader);
		bool									CreateLibPipelineState(const char* inFileName, const wchar_t* inLibName, Shader& ioShader);
		bool									CompileShader(Shader& ioShader);
		ComPtr<IDxcBlob>						Compile(const char* inFilename, const char* inEntryPoint, std::string_view inProfile);

		ComPtr<ID3D12StateObject>				CreateStateObject(IDxcBlob* inBlob, Shader& ioShader);
		ShaderTable								CreateShaderTable(const Shader& inShader);
		Shader									CombineShader(const Shader& inBaseShader, std::span<Shader> inCollections);

		HMODULE									mDxcompilerDll = NULL;
		ComPtr<IDxcUtils>						mDxcUtils;
		ComPtr<IDxcCompiler>					mDxcCompiler;
		ComPtr<IDxcIncludeHandler>				mDxcIncludeHandler;
		ComPtr<slang::IGlobalSession>			mGlobalSession;
	};
	Compiler mCompiler;

	void										Initialize();
	void										Finalize();

	void										Render(ID3D12GraphicsCommandList4* inCommandList);

	void										ImGuiShowTextures();

	void										InitializeScreenSizeTextures();
	void										FinalizeScreenSizeTextures();

	void										InitializeShaders();
	void										FinalizeShaders();

	void										SetHeaps()
	{
		// Heaps of Dynamic Resources needs to be set before RootSignature
		// See https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html#setdescriptorheaps-and-setrootsignature
		ID3D12DescriptorHeap* bindless_heaps[] =
		{
			gGetFrameContext().mViewDescriptorHeap.mHeap.Get(),
			gGetFrameContext().mSamplerDescriptorHeap.mHeap.Get(),
		};
		gCommandList->SetDescriptorHeaps(gArraySize(bindless_heaps), bindless_heaps);
	}

	void										ClearUnorderedAccessViewFloat(const Texture& inTexture)
	{
		SetHeaps();
		
		D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(inTexture.mUAVIndex);
		D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = gGetFrameContext().mClearDescriptorHeap.GetCPUHandle(inTexture.mUAVIndex);
		float4 clear_value = { 0, 0, 0, 0 };
		gCommandList->ClearUnorderedAccessViewFloat(gpu_handle, cpu_handle, inTexture.mResource.Get(), &clear_value.x, 0, nullptr);
	}

	void										Setup(const Shader& inShader, const RootConstants& inRootConstants = {})
	{
		SetHeaps();

		if (inShader.mCSName == nullptr)
			gCommandList->SetGraphicsRootSignature(mCompiler.mCommonRootSignature.Get());
		else
			gCommandList->SetComputeRootSignature(mCompiler.mCommonRootSignature.Get());

		if (inShader.mData.mStateObject != nullptr)
			gCommandList->SetPipelineState1(inShader.mData.mStateObject.Get());
		else
			gCommandList->SetPipelineState(inShader.mData.mPipelineState.Get());

		// See CreateCommonRootSignature for layout
		if (inShader.mCSName == nullptr)
		{
			gCommandList->SetGraphicsRoot32BitConstants(ROOT_CONSTANTS_REGISTER, ROOT_CONSTANTS_NUM_32BIT, &inRootConstants, 0);
			gCommandList->SetGraphicsRootConstantBufferView(ROOT_CBV_REGISTER, mRuntime.mConstantsBuffer.mResource->GetGPUVirtualAddress());
		}
		else
		{
			gCommandList->SetComputeRoot32BitConstants(ROOT_CONSTANTS_REGISTER, ROOT_CONSTANTS_NUM_32BIT, &inRootConstants, 0);
			gCommandList->SetComputeRootConstantBufferView(ROOT_CBV_REGISTER, mRuntime.mConstantsBuffer.mResource->GetGPUVirtualAddress());
		}
	}

	bool										mReloadShader = false;
	bool										mReloadScene = false;
	bool										mDumpRayQuery = false;

	bool										mAccumulationFrameUnlimited = false;
	bool										mAccumulationPaused = false;
	int											mAccumulationFrameCount = 1;
	bool										mAccumulationResetRequested = false;

	bool										mSpatialCacheActiveOnce = false;
	bool										mSpatialCacheResetRequested = true;

	bool										mSequenceDumpPNG = false;
	bool										mSequenceCameraEnabled = true;
	int											mSequenceFrameRecording = -1;

	uint2										mScreenSize = { 1920, 1080 };
	uint2										mScreenSizeRequested = { 0, 0 };
};
extern Renderer									gRenderer;

struct GPUTiming
{
	UINT										mQueryHeapIndex = 0;
	UINT64										mTimestampFrequency = 0;

	UINT64 TimestampBegin(ID3D12GraphicsCommandList4* inCommandList)
	{
		UINT64 timestamp = gRenderer.mRuntime.mQueryBuffer.GetReadback<UINT64>(gGetFrameContextIndex())[mQueryHeapIndex];
		inCommandList->EndQuery(gQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, mQueryHeapIndex++);
		return timestamp;
	}

	void TimestampEnd(ID3D12GraphicsCommandList4* inCommandList, UINT64 inTimestampBegin, float* outDurationMSPtr)
	{
		UINT64 timestamp = gRenderer.mRuntime.mQueryBuffer.GetReadback<UINT64>(gGetFrameContextIndex())[mQueryHeapIndex];
		float durationMS = (timestamp - inTimestampBegin) * 1000.0f / mTimestampFrequency;
		if (outDurationMSPtr != nullptr) { *outDurationMSPtr = durationMS; }
		inCommandList->EndQuery(gQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, mQueryHeapIndex++);
	}

	void FrameEnd(ID3D12GraphicsCommandList4* inCommandList, ID3D12Resource* inReadbackResource)
	{
		inCommandList->ResolveQueryData(gQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, mQueryHeapIndex, inReadbackResource, 0);
		mQueryHeapIndex = 0;
	}
};
extern GPUTiming								gGPUTiming;

struct GPUTimingScope
{
	GPUTimingScope(ID3D12GraphicsCommandList4* inCommandList) : mCommandList(inCommandList) { mTimestampBegin = gGPUTiming.TimestampBegin(mCommandList); }
	~GPUTimingScope()							{ gGPUTiming.TimestampEnd(mCommandList, mTimestampBegin, mDurationMSPtr); }

	UINT64										mTimestampBegin = 0;
	float*										mDurationMSPtr = nullptr;
	ID3D12GraphicsCommandList4*					mCommandList = nullptr;
};
#define GPU_TIMING_SCOPE(inName, inCommandList, outDurationMSPtr)		\
		PIXScopedEvent(inCommandList, PIX_COLOR(0, 255, 0), inName);	\
		GPUTimingScope mGPUTimingScope_##__LINE__(inCommandList); mGPUTimingScope_##__LINE__.mDurationMSPtr = outDurationMSPtr;