#include "Common.h"
#include "ImGui/imgui_impl_dx12.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3d12")
#pragma comment(lib, "dxgi")		// CreateDeviceD3D()
#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.
#pragma comment(lib, "dxguid")		// For DXGI_DEBUG_ALL
#endif

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

HMODULE								gPIXHandle = nullptr;

// Application
ComPtr<ID3D12Resource>				gConstantGPUBuffer = nullptr;

// Frame
FrameContext						gFrameContexts[NUM_FRAMES_IN_FLIGHT] = {};
glm::uint32							gFrameIndex = 0;
Constants							gConstants = {};

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
	gAssert(mSRVIndex != ViewDescriptorIndex::Count); // Need SRV for visualization
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

namespace ImGui 
{
	void Textures(std::span<Texture> inTextures, const std::string& inName, ImGuiTreeNodeFlags inFlags)
	{
		ImGui::Begin("Textures");

		if (ImGui::TreeNodeEx(inName.c_str(), inFlags))
		{
			static Texture* sTexture = nullptr;
			auto add_texture = [&](Texture& inTexture)
			{
				ImVec2 uv0 = ImVec2(0, 0);
				ImVec2 uv1 = ImVec2(1, 1);

				UINT64 handle = gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(inTexture.mSRVIndex).ptr;
				if (inTexture.mDepth != 1)
					handle |= ImGui_ImplDX12_ImTextureID_Mask_3D;

				ImGui::Image(reinterpret_cast<ImTextureID>(handle), ImVec2(inTexture.mWidth * inTexture.mUIScale, inTexture.mHeight * inTexture.mUIScale), uv0, uv1);

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

				ImGui::PushItemWidth(100);

				static int visualize_slice = 0;
				static bool visualize_show_alpha = false;

				ImGui::Text("Image Options");
				ImGui::SliderFloat("Min", &ImGui_ImplDX12_ShaderContants.mMin, 0.0, 1.0f); ImGui::SameLine(); ImGui::SliderFloat("Max", &ImGui_ImplDX12_ShaderContants.mMax, 0.0, 1.0f);
				ImGui::SliderInt("Depth Slice", &visualize_slice, 0, sTexture->mDepth - 1);
				ImGui::Checkbox("Show Alpha", &visualize_show_alpha);

				ImGui_ImplDX12_ShaderContants.mSlice = visualize_slice * 1.0f;
				ImGui_ImplDX12_ShaderContants.mAlpha = visualize_show_alpha ? 1.0f : 0.0f;

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