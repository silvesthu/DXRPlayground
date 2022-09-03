#include "Common.h"
#include "ImGui/imgui_impl_dx12.h"

// System
ID3D12Device5*						gDevice = nullptr;
ID3D12DescriptorHeap*				gRTVDescriptorHeap = nullptr;
ID3D12CommandQueue*					gCommandQueue = nullptr;
ID3D12GraphicsCommandList4*			gCommandList = nullptr;

ID3D12Fence*						gIncrementalFence = nullptr;
HANDLE                       		gIncrementalFenceEvent = nullptr;
UINT64                       		gFenceLastSignaledValue = 0;

IDXGISwapChain3*					gSwapChain = nullptr;
HANDLE                       		gSwapChainWaitableObject = nullptr;
ID3D12Resource*						gBackBufferRenderTargetResource[NUM_BACK_BUFFERS] = {};
D3D12_CPU_DESCRIPTOR_HANDLE			gBackBufferRenderTargetRTV[NUM_BACK_BUFFERS] = {};

// Application
ComPtr<ID3D12Resource>				gConstantGPUBuffer = nullptr;

ComPtr<ID3D12RootSignature>			gDXRGlobalRootSignature = nullptr;
ComPtr<ID3D12StateObject>			gDXRStateObject = nullptr;
ShaderTable							gDXRShaderTable = {};

bool								gUseDXRRayQueryShader = true;
Shader								gDXRRayQueryShader = Shader().FileName("Shader/Raytracing.hlsl").CSName("RayQueryCS");

Shader								gDiffTexture2DShader = Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture2DShader");
Shader								gDiffTexture3DShader = Shader().FileName("Shader/DiffTexture.hlsl").CSName("DiffTexture3DShader");

Shader								gCompositeShader = Shader().FileName("Shader/Composite.hlsl").VSName("ScreenspaceTriangleVS").PSName("CompositePS");

// Frame
FrameContext						gFrameContexts[NUM_FRAMES_IN_FLIGHT] = {};
glm::uint32							gFrameIndex = 0;
Constants							gConstants = {};

// Renderer
void Renderer::ReleaseResources()
{
	for (auto&& texture : mRuntime.mTextures)
		texture.ReleaseResources();
}

void Renderer::ImGuiShowTextures()
{
	ImGui::Textures(mRuntime.mTextures, "Renderer", ImGuiTreeNodeFlags_None);
}

Renderer							gRenderer;

// Capture
Texture*							gDumpTexture = nullptr;
Texture								gDumpTextureProxy = {};

int Texture::GetPixelSize() const
{
	return static_cast<int>(DirectX::BitsPerPixel(mFormat) / 8);
}

glm::uint64 Texture::GetSubresourceSize() const
{
	return GetRequiredIntermediateSize(mResource.Get(), 0, mSubresourceCount);
}

void Texture::Initialize()
{
	D3D12_RESOURCE_DESC resource_desc = gGetTextureResourceDesc(mWidth, mHeight, mDepth, mFormat);
	D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mResource)));
	gSetName(mResource, "", mName);

	// SRV
	if (mSRVIndex != ViewDescriptorIndex::Count)
	{
		for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.Format = mSRVFormat != DXGI_FORMAT_UNKNOWN ? mSRVFormat : mFormat;
			desc.ViewDimension = mDepth == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE3D;
			desc.Texture2D.MipLevels = (UINT)-1;
			desc.Texture2D.MostDetailedMip = 0;
			desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			gDevice->CreateShaderResourceView(mResource.Get(), &desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(mSRVIndex));
		}
	}

	// UAV
	if (mUAVIndex != ViewDescriptorIndex::Count)
	{
		for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
			if (resource_desc.DepthOrArraySize == 1)
			{
				desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MipSlice = 0;
				desc.Texture2D.PlaneSlice = 0;
			}
			else
			{
				desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
				desc.Texture3D.MipSlice = 0;
				desc.Texture3D.FirstWSlice = 0;
				desc.Texture3D.WSize = resource_desc.DepthOrArraySize;
			}
			gDevice->CreateUnorderedAccessView(mResource.Get(), nullptr, &desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(mUAVIndex));
		}
	}
	
	// SRV for ImGui
	{
		if (mImGuiTextureIndex == -1)
			mImGuiTextureIndex = ImGui_ImplDX12_AllocateTexture(resource_desc);

		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = mFormat;
		if (mDepth == 1)
		{
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipLevels = (UINT)-1;
			desc.Texture2D.MostDetailedMip = 0;
		}
		else
		{
			desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			desc.Texture3D.MipLevels = (UINT)-1;
			desc.Texture3D.MostDetailedMip = 0;
		}
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		gDevice->CreateShaderResourceView(mResource.Get(), &desc, ImGui_ImplDX12_TextureCPUHandle(mImGuiTextureIndex));
	}

	// UIScale
	if (mUIScale == 0.0f)
		mUIScale = 256.0f / static_cast<float>(mWidth);
}

void Texture::Update()
{
	// Upload from file
	if (!mLoaded && mPath != nullptr)
	{
		// Load file
		DirectX::ScratchImage scratch_image;
		HRESULT hr = E_FAIL;
		if (FAILED(hr))
			hr = DirectX::LoadFromTGAFile(mPath, nullptr, scratch_image);
		if (FAILED(hr))
			hr = DirectX::LoadFromDDSFile(mPath, DirectX::DDS_FLAGS_NONE, nullptr, scratch_image);
		gAssert(!FAILED(hr));

		// Upload
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		PrepareUpload(gDevice, scratch_image.GetImages(), scratch_image.GetImageCount(), scratch_image.GetMetadata(), subresources);
		InitializeUpload();
		BarrierScope expected_scope(gCommandList, mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		UpdateSubresources(gCommandList, mResource.Get(), mUploadResource.Get(), 0, 0, mSubresourceCount, subresources.data());

		mLoaded = true;
	}

	// Upload from raw data
	if (!mUploadData.empty())
	{
		D3D12_SUBRESOURCE_DATA subresource = {};
		subresource.pData = mUploadData.data();
		subresource.RowPitch = mWidth * GetPixelSize();
		subresource.SlicePitch = mWidth * mHeight * GetPixelSize();
		InitializeUpload();
		BarrierScope expected_scope(gCommandList, mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		UpdateSubresources(gCommandList, mResource.Get(), mUploadResource.Get(), 0, 0, mSubresourceCount, &subresource);

		mUploadData.clear();
	}
}

void Texture::InitializeUpload()
{
	if (mUploadResource != nullptr)
		return;
	
	UINT64 byte_count = GetSubresourceSize();
	D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(byte_count);
	D3D12_HEAP_PROPERTIES upload_properties = gGetUploadHeapProperties();
	gValidate(gDevice->CreateCommittedResource(&upload_properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mUploadResource)));
	std::wstring name = gToWString(mName);
	mUploadResource->SetName(name.c_str());
}

void Texture::ReleaseResources()
{
	mResource = nullptr;
	mUploadResource = nullptr;
}

namespace ImGui 
{
	void Textures(std::span<Texture> inTextures, const std::string& inName, ImGuiTreeNodeFlags inFlags)
	{
		ImGui::Begin("Textures");

		static float ui_scale = 1.0f;
		static bool flip_y = false;

		if (ImGui::TreeNodeEx(inName.c_str(), inFlags))
		{
			static Texture* sTexture = nullptr;
			auto add_texture = [&](Texture& inTexture)
			{
				ImVec2 uv0 = ImVec2(0, 0);
				ImVec2 uv1 = ImVec2(1, 1);

				if (flip_y)
					std::swap(uv0.y, uv1.y);

				float item_ui_scale = inTexture.mUIScale * ui_scale;
				ImGui::Image(reinterpret_cast<ImTextureID>(ImGui_ImplDX12_TextureGPUHandle(inTexture.mImGuiTextureIndex).ptr), ImVec2(inTexture.mWidth * item_ui_scale, inTexture.mHeight * item_ui_scale), uv0, uv1);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					sTexture = &inTexture;
					ImGui::OpenPopup("Image Options");
				}

				ImGui::SameLine();
				std::string text = "-----------------------------------\n";
				text += inTexture.mName;
				text += "\n";
				text += std::to_string(inTexture.mWidth) + " x " + std::to_string(inTexture.mHeight) + (inTexture.mDepth == 1 ? "" : " x " + std::to_string(inTexture.mDepth));
				text += "\n";
				text += nameof::nameof_enum(inTexture.mFormat);
				if (inTexture.mSRVFormat != DXGI_FORMAT_UNKNOWN)
				{
					text += "\n";
					text += nameof::nameof_enum(inTexture.mSRVFormat);
					text += " (SRV)";
				}
				ImGui::Text(text.c_str());
			};

			if (ImGui::BeginPopup("Image Options"))
			{
				gAssert(sTexture != nullptr);

				ImGui::Text(sTexture->mName);

				if (ImGui::Button("Dump"))
					gDumpTexture = sTexture;

				ImGui::Separator();

				ImGui_ImplDX12_ShowTextureOption(sTexture->mImGuiTextureIndex);

				ImGui::Separator();

				ImGui::Text("GUI Options (Global)");
				ImGui::PushItemWidth(100);
				ImGui::SliderFloat("UI Scale", &ui_scale, 1.0, 4.0f);
				ImGui::SameLine();
				ImGui::Checkbox("Flip Y", &flip_y);
				ImGui::PopItemWidth();

				ImGui::EndPopup();
			}

			for (auto&& texture : inTextures)
				add_texture(texture);

			ImGui::TreePop();
		}

		ImGui::End();
	}
}