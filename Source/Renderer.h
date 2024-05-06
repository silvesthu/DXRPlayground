#pragma once

#include "Common.h"

struct Renderer
{
	struct Runtime : RuntimeBase<Runtime>
	{
		Shader									mRayQueryShader				= Shader().FileName("Shader/RayQuery.hlsl").CSName("RayQueryCS");
		Shader									mClearShader				= Shader().FileName("Shader/Composite.hlsl").CSName("ClearCS");
		Shader									mDiffTexture2DShader		= Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture2DShader");
		Shader									mDiffTexture3DShader		= Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture3DShader");
		Shader									mCompositeShader			= Shader().FileName("Shader/Composite.hlsl").VSName("ScreenspaceTriangleVS").PSName("CompositePS");
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
		Texture									mScreenReservoirTexture		= Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenReservoirUAV).SRVIndex(ViewDescriptorIndex::ScreenReservoirSRV).Name("Renderer.ScreenReservoirTexture");

		Texture									mScreenSentinelTexture;
		std::span<Texture>						mScreenTextures = std::span<Texture>(&mScreenColorTexture, &mScreenSentinelTexture);

		Texture									mUVCheckerMap = Texture().Width(1024).Height(1024).Format(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).SRVIndex(ViewDescriptorIndex::UVCheckerMap).Name("Renderer.UVCheckerMap").Path(L"Asset/UVChecker-map/UVCheckerMaps/UVCheckerMap01-1024.png");
		Texture									mIESTexture = Texture().Width(256).Height(16).Format(DXGI_FORMAT_R32_FLOAT).SRVIndex(ViewDescriptorIndex::IESSRV).Name("Renderer.IES").Path(L"Asset/IES/007cfb11e343e2f42e3b476be4ab684e/IES.hdr");

		Texture									mSentinelTexture;
		std::span<Texture>						mTextures = std::span<Texture>(&mUVCheckerMap, &mSentinelTexture);

		ComPtr<ID3D12Resource>					mBackBuffers[NUM_BACK_BUFFERS] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE				mBufferBufferRTVs[NUM_BACK_BUFFERS] = {};
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

	void										Setup(const Shader& inShader)
	{
		Setup(inShader.mData, inShader.mCSName == nullptr);
	}

	void										Setup(const Shader::Data& inShaderData, bool inGraphics = false)
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
			gCommandList->SetGraphicsRootConstantBufferView((int)RootParameterIndex::Constants, gConstantBuffer->GetGPUVirtualAddress());
		else
			gCommandList->SetComputeRootConstantBufferView((int)RootParameterIndex::Constants, gConstantBuffer->GetGPUVirtualAddress());
	}

	bool										mReloadShader = false;
	bool										mDumpRayQuery = false;
	bool										mTestLibShader = false;
	bool										mTestMultipleHitShaders = false;

	bool										mAccumulationDone = false;
	bool										mAccumulationFrameUnlimited = false;
	bool										mAccumulationPaused = false;
	uint32_t									mAccumulationFrameCount = 64;
	bool										mAccumulationResetRequested = false;

	int											mResizeWidth = 0;
	int											mResizeHeight = 0;
};
extern Renderer									gRenderer;