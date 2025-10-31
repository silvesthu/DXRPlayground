#include "Renderer.h"
#include "Atmosphere.h"
#include "Cloud.h"
#include "Renderer.inl"

void Renderer::Initialize()
{
	gInitializeDxcInterfaces();

	for (auto&& texture : mRuntime.mTextures)
		texture.Initialize();
	
	for (auto&& buffer : mRuntime.mBuffers)
		buffer.Initialize();

	InitializeShaders();
	InitializeScreenSizeTextures();
}

void Renderer::Finalize()
{
	mRuntime.Reset();
	gFinalizeDxcInterfaces();
}

void Renderer::Render()
{
	for (auto&& texture : mRuntime.mTextures)
		texture.Update();
}

void Renderer::ImGuiShowTextures()
{
	ImGui::Textures(mRuntime.mScreenTextures, "Renderer.Screen", ImGuiTreeNodeFlags_None);
	ImGui::Textures(mRuntime.mTextures, "Renderer", ImGuiTreeNodeFlags_None);
}

void Renderer::InitializeScreenSizeTextures()
{
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
	gSwapChain->GetDesc1(&swap_chain_desc);

	for (int i = 0; i < kFrameInFlightCount; i++)
	{
		gSwapChain->GetBuffer(i, IID_PPV_ARGS(mRuntime.mBackBuffers[i].mResource.GetAddressOf()));
		std::wstring name = gToWString(mRuntime.mBackBuffers[i].mName + gToString(i));
		mRuntime.mBackBuffers[i].mResource->SetName(name.c_str());

		mRuntime.mBackBuffers[i].mWidth = swap_chain_desc.Width;
		mRuntime.mBackBuffers[i].mHeight = swap_chain_desc.Height;
		mRuntime.mBackBuffers[i].mFormat = swap_chain_desc.Format;

		RTVDescriptorIndex index = RTVDescriptorIndex((uint)mRuntime.mBackBuffers[i].mRTVIndex);
		D3D12_CPU_DESCRIPTOR_HANDLE handle = gCPUContext.mRTVDescriptorHeap.GetCPUHandle(index);
		gDevice->CreateRenderTargetView(mRuntime.mBackBuffers[i].mResource.Get(), nullptr, handle);
	}

	for (auto&& screen_texture : mRuntime.mScreenTextures)
		screen_texture.Width(swap_chain_desc.Width).Height(swap_chain_desc.Height).Initialize();

	mScreenWidth = swap_chain_desc.Width;
	mScreenHeight = swap_chain_desc.Height;
}

void Renderer::FinalizeScreenSizeTextures()
{
	for (int i = 0; i < kFrameInFlightCount; i++)
		mRuntime.mBackBuffers[i].mResource = nullptr;
}

void Renderer::InitializeShaders()
{
	for (auto&& shader : gRenderer.mRuntime.mShaders)
		gCreatePipelineState(shader);

	gRenderer.mRuntime.mLibLocalRootSignature = gCreateLocalRootSignature();
	gCreatePipelineState(gRenderer.mRuntime.mRayGenerationShader);
	for (auto&& shader : gRenderer.mRuntime.mCollectionShaders)
		gCreatePipelineState(shader);
	gCombineShader(gRenderer.mRuntime.mRayGenerationShader, gRenderer.mRuntime.mCollectionShaders, gRenderer.mRuntime.mLibShader);
	gRenderer.mRuntime.mLibShaderTable = gCreateShaderTable(gRenderer.mRuntime.mLibShader);

	for (auto&& shaders : gAtmosphere.mRuntime.mShadersSet)
		for (auto&& shader : shaders)
			gCreatePipelineState(shader);

	for (auto&& shader : gCloud.mRuntime.mShaders)
		gCreatePipelineState(shader);
}

void Renderer::FinalizeShaders()
{
	// No actual cleanup in case rebuild fails
}

Renderer gRenderer;
Timing gTiming;