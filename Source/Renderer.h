#pragma once

#include "Common.h"

struct Renderer
{
	struct Runtime : RuntimeBase<Runtime>
	{
		Shader								mRayQueryShader = Shader().FileName("Shader/Raytracing.hlsl").CSName("RayQueryCS");
		Shader								mDiffTexture2DShader = Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture2DShader");
		Shader								mDiffTexture3DShader = Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture3DShader");
		Shader								mCompositeShader = Shader().FileName("Shader/Composite.hlsl").VSName("ScreenspaceTriangleVS").PSName("CompositePS");

		Shader								mSentinelShader = Shader();
		std::span<Shader>					mShaders = std::span<Shader>(&mRayQueryShader, &mSentinelShader);

		Texture								mScreenColorTexture = Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenColorUAV).SRVIndex(ViewDescriptorIndex::ScreenColorSRV).Name("Renderer.ScreenColorTexture");
		Texture								mScreenDebugTexture = Texture().Format(DXGI_FORMAT_R32G32B32A32_FLOAT).UAVIndex(ViewDescriptorIndex::ScreenDebugUAV).SRVIndex(ViewDescriptorIndex::ScreenDebugSRV).Name("Renderer.ScreenDebugTexture");

		Texture								mScreenSentinelTexture;
		std::span<Texture>					mScreenTextures = std::span<Texture>(&mScreenColorTexture, &mScreenSentinelTexture);

		Texture								mIESTexture = Texture().Width(256).Height(16).Format(DXGI_FORMAT_R32_FLOAT).SRVIndex(ViewDescriptorIndex::IESSRV).Name("Renderer.IES").Path(L"Asset/IES/007cfb11e343e2f42e3b476be4ab684e/IES.hdr");

		Texture								mSentinelTexture;
		std::span<Texture>					mTextures = std::span<Texture>(&mIESTexture, &mSentinelTexture);

		ComPtr<ID3D12Resource>				mBackBuffers[NUM_BACK_BUFFERS] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE			mBufferBufferRTVs[NUM_BACK_BUFFERS] = {};
	};
	Runtime mRuntime;

	void									Initialize();
	void									Finalize();

	void									Render();

	void									ImGuiShowTextures();

	void									InitializeScreenSizeTextures();
	void									FinalizeScreenSizeTextures();

	void									InitializeShaders();
	void									FinalizeShaders();

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

	bool									mReloadShader = false;
	bool									mDumpDisassemblyRayQuery = false;

	bool									mAccumulationDone = false;
	bool									mAccumulationFrameInfinity = false;
	glm::uint32								mAccumulationFrameCount = 64;
	bool									mAccumulationResetRequested = false;
};
extern Renderer								gRenderer;