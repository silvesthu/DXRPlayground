#pragma once

#include "Common.h"

static constexpr DXGI_FORMAT					kBackBufferFormat			= DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr uint							kTimestampCount				= 1024;

struct Renderer
{
	struct Runtime : RuntimeBase<Runtime>
	{
		Shader									mRayQueryShader				= Shader().FileName("Shader/RayQuery.hlsl").CSName("RayQueryCS");
		Shader									mDepthShader				= Shader().FileName("Shader/RayQuery.hlsl").VSName("ScreenspaceTriangleVS").PSName("DepthPS").DepthWrite(true).DSVFormat(DXGI_FORMAT_D32_FLOAT);
		Shader									mPrepareLightsShader		= Shader().FileName("Shader/PrepareLights.hlsl").CSName("PrepareLightsCS");
		Shader									mClearShader				= Shader().FileName("Shader/Composite.hlsl").CSName("ClearCS");
		Shader									mGenerateTextureShader		= Shader().FileName("Shader/Composite.hlsl").CSName("GeneratTextureCS");
		Shader									mBRDFSliceShader			= Shader().FileName("Shader/Composite.hlsl").CSName("BRDFSliceCS");
		Shader									mReadbackShader				= Shader().FileName("Shader/Composite.hlsl").CSName("ReadbackCS");
		Shader									mDiffTexture2DShader		= Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture2DShader");
		Shader									mDiffTexture3DShader		= Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture3DShader");
		Shader									mLineShader					= Shader().FileName("Shader/Composite.hlsl").VSName("LineVS").PSName("LinePS").Topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE).DepthFunc(D3D12_COMPARISON_FUNC_LESS).RTVFormat(kBackBufferFormat).DSVFormat(DXGI_FORMAT_D32_FLOAT);
		Shader									mLineHiddenShader			= Shader().FileName("Shader/Composite.hlsl").VSName("LineVS").PSName("LineHiddenPS").Topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE).DepthFunc(D3D12_COMPARISON_FUNC_GREATER).RTVFormat(kBackBufferFormat).DSVFormat(DXGI_FORMAT_D32_FLOAT);
		Shader									mCompositeShader			= Shader().FileName("Shader/Composite.hlsl").VSName("ScreenspaceTriangleVS").PSName("CompositePS").RTVFormat(kBackBufferFormat).DSVFormat(DXGI_FORMAT_D32_FLOAT);
		Shader									mSentinelShader				= Shader();
		std::span<Shader>						mShaders					= std::span<Shader>(&mRayQueryShader, &mSentinelShader);

		Shader									mRayGenerationShader		= Shader().FileName("Shader/RayGeneration.hlsl").RayGenerationName(L"RayGeneration").RootSignatureReference(&mRayQueryShader);
		Shader									mMissShader					= Shader().FileName("Shader/Miss.hlsl").MissName(L"Miss").RootSignatureReference(&mRayQueryShader);
		Shader									mAnyHitShader				= Shader().FileName("Shader/AnyHit.hlsl").AnyHitName(L"AnyHit").RootSignatureReference(&mRayQueryShader);														// AnyHit MUST comes before HitGroup referencing it when being AddToStateObject, otherwise DXGI_ERROR_DRIVER_INTERNAL_ERROR
		Shader									mClosestHit100Shader		= Shader().FileName("Shader/ClosestHit100.hlsl").ClosestHitName(L"ClosestHit100").RootSignatureReference(&mRayQueryShader);										// ClosestHit without AnyHit
		Shader									mClosestHit010Shader		= Shader().FileName("Shader/ClosestHit010.hlsl").ClosestHitName(L"ClosestHit010").AnyHitReference(&mAnyHitShader).RootSignatureReference(&mRayQueryShader);		// ClosestHit with AnyHit in different library
		Shader									mClosestHit001Shader		= Shader().FileName("Shader/ClosestHit001.hlsl").ClosestHitName(L"ClosestHit001").AnyHitName(L"AnyHit001").RootSignatureReference(&mRayQueryShader);			// ClosestHit with AnyHit in same library
		Shader									mCollectionSentinelShader	= Shader();
		std::span<Shader>						mCollectionShaders			= std::span<Shader>(&mMissShader,			&mCollectionSentinelShader);
		std::span<Shader>						mHitGroupShaders			= std::span<Shader>(&mClosestHit100Shader,	&mCollectionSentinelShader);
		Shader									mLibShader					= Shader();
		ComPtr<ID3D12RootSignature>				mLibLocalRootSignature;
		ShaderTable								mLibShaderTable;

		Texture									mScreenColorTexture			= Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenColorUAV).SRVIndex(ViewDescriptorIndex::ScreenColorSRV).Name("Renderer.ScreenColorTexture");
		Texture									mScreenDebugTexture			= Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenDebugUAV).SRVIndex(ViewDescriptorIndex::ScreenDebugSRV).Name("Renderer.ScreenDebugTexture");
		Texture									mScreenReadbackTexture		= Texture().Format(DXGI_FORMAT_R8G8B8A8_UNORM).UAVIndex(ViewDescriptorIndex::ScreenReadbackUAV).SRVIndex(ViewDescriptorIndex::ScreenReadbackSRV).Name("Renderer.ScreenReadbackTexture");
		Texture									mScreenDepthTexture			= Texture().Format(DXGI_FORMAT_D32_FLOAT).DSVIndex(DSVDescriptorIndex::ScreenDepth).SRVIndex(ViewDescriptorIndex::ScreenDepthSRV).SRVFormat(DXGI_FORMAT_R32_FLOAT).Name("Renderer.ScreenDepthTexture");
		Texture									mScreenReservoirTexture		= Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenReservoirUAV).SRVIndex(ViewDescriptorIndex::ScreenReservoirSRV).Name("Renderer.ScreenReservoirTexture");

		Texture									mScreenSentinelTexture;
		std::span<Texture>						mScreenTextures				= std::span<Texture>(&mScreenColorTexture, &mScreenSentinelTexture);

		Texture									mBRDFSliceTexture			= Texture().Width(512).Height(512).Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::BRDFSliceUAV).SRVIndex(ViewDescriptorIndex::BRDFSliceSRV).Name("Renderer.BRDFSlice");
		Texture									mGenerateTexture			= Texture().Width(2 * 20).Height(2 * 80).Format(DXGI_FORMAT_R8G8B8A8_UNORM).UAVIndex(ViewDescriptorIndex::GeneratedUAV).SRVIndex(ViewDescriptorIndex::GeneratedSRV).Name("Renderer.Generated");
		// Texture									mUVCheckerTexture			= Texture().Width(1024).Height(1024).Format(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).SRVIndex(ViewDescriptorIndex::UVCheckerSRV).Name("Renderer.UVCheckerMap").Path(L"Asset/UVChecker-map/UVCheckerMaps/UVCheckerMap01-1024.png");
		// Texture									mIESTexture					= Texture().Width(256).Height(16).Format(DXGI_FORMAT_R32_FLOAT).SRVIndex(ViewDescriptorIndex::IESSRV).Name("Renderer.IES").Path(L"Asset/IES/007cfb11e343e2f42e3b476be4ab684e/IES.hdr");

		Texture									mSentinelTexture;
		std::span<Texture>						mTextures					= std::span<Texture>(&mBRDFSliceTexture, &mSentinelTexture);

		Texture									mBackBuffers[kFrameInFlightCount] = { 
																			Texture().Format(kBackBufferFormat).RTVIndex(RTVDescriptorIndex::BackBuffer0).Name("Renderer.BackBuffer0"),
																			Texture().Format(kBackBufferFormat).RTVIndex(RTVDescriptorIndex::BackBuffer1).Name("Renderer.BackBuffer1") };

		Buffer									mConstantsBuffer			= Buffer().ByteCount(sizeof(Constants)).CBVIndex(ViewDescriptorIndex::ConstantsCBV).Name("Constants").Upload(true);
		Buffer									mPixelInspectionBuffer		= Buffer().ByteCount(sizeof(PixelInspection)).UAVIndex(ViewDescriptorIndex::PixelInspectionUAV).Name("PixelInspection").Readback(true);
		Buffer									mRayInspectionBuffer		= Buffer().ByteCount(sizeof(RayInspection)).UAVIndex(ViewDescriptorIndex::RayInspectionUAV).Name("RayInspection");
		Buffer									mQueryBuffer				= Buffer().ByteCount(sizeof(UINT64) * kTimestampCount).Name("Query").GPU(false).Readback(true);

		Buffer									mSentinelBuffer;
		std::span<Buffer>						mBuffers					= std::span<Buffer>(&mConstantsBuffer, &mSentinelBuffer);
	};
	Runtime mRuntime;

	void										Initialize();
	void										Finalize();

	void										Render();

	void										Resize(int inWidth, int inHeight)
	{
		mResizeWidth = inWidth;
		mResizeHeight = inHeight;
	}

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

	void										Setup(const Shader& inShader)
	{
		SetHeaps();

		if (inShader.mCSName == nullptr)
			gCommandList->SetGraphicsRootSignature(inShader.mData.mRootSignature.Get());
		else
			gCommandList->SetComputeRootSignature(inShader.mData.mRootSignature.Get());

		if (inShader.mData.mStateObject != nullptr)
			gCommandList->SetPipelineState1(inShader.mData.mStateObject.Get());
		else
			gCommandList->SetPipelineState(inShader.mData.mPipelineState.Get());

		// Root parameters need to be set after RootSignature
		if (inShader.mCSName == nullptr)
			gCommandList->SetGraphicsRootConstantBufferView((int)RootParameterIndex::Constants, mRuntime.mConstantsBuffer.mResource->GetGPUVirtualAddress());
		else
			gCommandList->SetComputeRootConstantBufferView((int)RootParameterIndex::Constants, mRuntime.mConstantsBuffer.mResource->GetGPUVirtualAddress());
	}

	bool										mReloadShader = false;
	bool										mDumpRayQuery = false;
	bool										mTestLibShader = false;
	bool										mTestMultipleHitShaders = false;

	bool										mAccumulationFrameUnlimited = false;
	bool										mAccumulationPaused = false;
	uint										mAccumulationFrameCount = 64;
	bool										mAccumulationResetRequested = false;

	uint										mScreenWidth = kScreenWidth;
	uint										mScreenHeight = kScreenHeight;
	uint										mResizeWidth = 0;
	uint										mResizeHeight = 0;
};
extern Renderer									gRenderer;

struct Timing
{
	UINT										mQueryHeapIndex = 0;
	UINT64										mTimestampFrequency = 0;

	UINT64 TimestampBegin()
	{
		UINT64 timestamp = gRenderer.mRuntime.mQueryBuffer.ReadbackAs<UINT64>(gGetFrameContextIndex())[mQueryHeapIndex];
		gCommandList->EndQuery(gQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, mQueryHeapIndex++);
		return timestamp;
	}

	void TimestampEnd(UINT64 inTimestampBegin, float& outMS)
	{
		UINT64 timestamp = gRenderer.mRuntime.mQueryBuffer.ReadbackAs<UINT64>(gGetFrameContextIndex())[mQueryHeapIndex];
		outMS = (timestamp - inTimestampBegin) * 1000.0f / mTimestampFrequency;
		gCommandList->EndQuery(gQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, mQueryHeapIndex++);
	}

	void FrameEnd(ID3D12Resource* inReadbackResource)
	{
		gCommandList->ResolveQueryData(gQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, mQueryHeapIndex, inReadbackResource, 0);
		mQueryHeapIndex = 0;
	}
};
extern Timing									gTiming;

struct TimingScope
{
	TimingScope(float& outMS) : mOutMS(outMS)	{ mTimestampBegin = gTiming.TimestampBegin(); }
	~TimingScope()								{ gTiming.TimestampEnd(mTimestampBegin, mOutMS); }

	UINT64										mTimestampBegin = 0;
	float&										mOutMS;
};
#define TIMING_SCOPE(name, outMS)				PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), name);				\
												TimingScope mTimingScope_##__LINE__(outMS)