#include "Renderer.h"
#include "Atmosphere.h"
#include "Cloud.h"
#include "Renderer.inl"

void Renderer::Initialize()
{
	gInitializeDxcInterfaces();

	InitializeShaders();
	InitializeScreenSizeTextures();

	for (auto&& texture : mRuntime.mTextures)
		texture.Initialize();
}

void Renderer::Finalize()
{
	FinalizeScreenSizeTextures();

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

	// Lib Shader
	uint32_t local_root_signature_register_space = 100;
	D3D12_DESCRIPTOR_RANGE local_root_descriptor_table_range =
	{
		.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		.NumDescriptors = 4096, // Unbounded, match size with referencing FrameContext::mViewDescriptorHeap
		.BaseShaderRegister = 0,
		.RegisterSpace = local_root_signature_register_space,
		.OffsetInDescriptorsFromTableStart = 0,
	};
	D3D12_ROOT_PARAMETER local_root_parameters[] =
	{
		D3D12_ROOT_PARAMETER { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .RegisterSpace = local_root_signature_register_space, .Num32BitValues = 4 } },
		D3D12_ROOT_PARAMETER { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV, .Descriptor = { .ShaderRegister = 1, .RegisterSpace = local_root_signature_register_space }  },
		D3D12_ROOT_PARAMETER { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = D3D12_ROOT_DESCRIPTOR_TABLE { .NumDescriptorRanges = 1, .pDescriptorRanges = &local_root_descriptor_table_range } },
	};
	gRenderer.mRuntime.mLibLocalRootSignature = gCreateRootSignature(
		D3D12_ROOT_SIGNATURE_DESC
		{ 
			.NumParameters = ARRAYSIZE(local_root_parameters),
			.pParameters = local_root_parameters,
			.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE,
		});
	gRenderer.mRuntime.mLibLocalRootSignature->SetName(L"LocalRootSignature");
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