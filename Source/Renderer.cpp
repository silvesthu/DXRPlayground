#include "Renderer.h"
#include "Atmosphere.h"
#include "Cloud.h"
#include "ShaderHelper.h"

void Renderer::Initialize()
{
	InitializeShaders();
	InitializeScreenSizeTextures();

	for (auto&& texture : mRuntime.mTextures)
		texture.Initialize();
}

void Renderer::Finalize()
{
	FinalizeScreenSizeTextures();

	mRuntime.Reset();
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
	for (int i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gSwapChain->GetBuffer(i, IID_PPV_ARGS(mRuntime.mBackBuffers[i].GetAddressOf()));
		std::wstring name = L"BackBuffer_" + std::to_wstring(i);
		mRuntime.mBackBuffers[i]->SetName(name.c_str());
		gDevice->CreateRenderTargetView(mRuntime.mBackBuffers[i].Get(), nullptr, mRuntime.mBufferBufferRTVs[i]);
	}

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
	gSwapChain->GetDesc1(&swap_chain_desc);

	for (auto&& screen_texture : mRuntime.mScreenTextures)
		screen_texture.Width(swap_chain_desc.Width).Height(swap_chain_desc.Height).Initialize();
}

void Renderer::FinalizeScreenSizeTextures()
{
	for (int i = 0; i < NUM_BACK_BUFFERS; i++)
		mRuntime.mBackBuffers[i] = nullptr;
}

void Renderer::InitializeShaders()
{
	for (auto&& shader : gRenderer.mRuntime.mShaders)
		gCreatePipelineState(shader);

	for (auto&& shaders : gAtmosphere.mRuntime.mShadersSet)
		for (auto&& shader : shaders)
			gCreatePipelineState(shader);

	for (auto&& shader : gCloud.mRuntime.mShaders)
		gCreatePipelineState(shader);

	// Lib Shader
	gCreatePipelineState(gRenderer.mRuntime.mLibBaseShader);
	if (gRenderer.mUseLibHitShader)
	{
		gCreatePipelineState(gRenderer.mRuntime.mLibHitShader);

		ComPtr<ID3D12StateObject> combined_state_object = gCombineLibStateObject(
			gRenderer.mRuntime.mLibBaseShader.mData.mStateObject.Get(),
			gRenderer.mRuntime.mLibHitShader.mData.mStateObject.Get());

		gRenderer.mRuntime.mLibShader.mData = gRenderer.mRuntime.mLibBaseShader.mData;
		gRenderer.mRuntime.mLibShader.mData.mStateObject = combined_state_object;
	}
	else
		gRenderer.mRuntime.mLibShader.mData = gRenderer.mRuntime.mLibBaseShader.mData;
	gRenderer.mRuntime.mLibShaderTable = gCreateShaderTable(gRenderer.mRuntime.mLibShader);
}

void Renderer::FinalizeShaders()
{
	// No actual cleanup in case rebuild fails
}

Renderer gRenderer;