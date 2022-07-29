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

ComPtr<ID3D12DescriptorHeap>		gUniversalHeap = nullptr;
SIZE_T								gUniversalHeapHandleIncrementSize = 0;
std::atomic<int>					gUniversalHeapHandleIndex = 0;

bool								gUseDXRInlineShader = true;
Shader								gDXRInlineShader = Shader().CSName(L"InlineRaytracingCS").UseGlobalRootSignature(true);

Shader								gDiffTexture2DShader = Shader().CSName(L"DiffTexture2DShader");
Shader								gDiffTexture3DShader = Shader().CSName(L"DiffTexture3DShader");

Shader								gCompositeShader = Shader().VSName(L"ScreenspaceTriangleVS").PSName(L"CompositePS");

// Frame
FrameContext						gFrameContext[NUM_FRAMES_IN_FLIGHT] = {};
glm::uint32							gFrameIndex = 0;
PerFrameConstants					gPerFrameConstantBuffer = {};

// Capture
Texture*							gDumpTexture = nullptr;
Texture								gDumpTextureProxy = {};

void Shader::InitializeDescriptors(const std::vector<Shader::DescriptorInfo>& inEntries)
{
	// DescriptorHeap
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 1000000; // Max
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		gValidate(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mData.mDescriptorHeap)));

		mData.mDescriptorHeap->SetName(mCSName != nullptr ? mCSName : mPSName);
	}

	// DescriptorTable
	if (!inEntries.empty())
	{
		// Check if root signature is supported
		const D3D12_ROOT_SIGNATURE_DESC* root_signature_desc = mData.mRootSignatureDeserializer->GetRootSignatureDesc();
		gAssert(root_signature_desc->NumParameters >= 1);
		gAssert(root_signature_desc->pParameters[0].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);

		const D3D12_ROOT_DESCRIPTOR_TABLE& table = root_signature_desc->pParameters[0].DescriptorTable;

		D3D12_CPU_DESCRIPTOR_HANDLE handle = mData.mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		UINT increment_size = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		int entry_index = 0;
		for (UINT i = 0; i < table.NumDescriptorRanges; i++)
		{
			const D3D12_DESCRIPTOR_RANGE& range = table.pDescriptorRanges[i];
			switch (range.RangeType)
			{
			case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
			{
				for (UINT j = 0; j < range.NumDescriptors; j++)
				{
					const DescriptorInfo& info = inEntries[entry_index];
					ID3D12Resource* resource = inEntries[entry_index].mResource;
					D3D12_RESOURCE_DESC resource_desc = inEntries[entry_index].mResource->GetDesc();

					D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
					desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					desc.Format = resource_desc.Format;
					switch (resource_desc.Dimension)
					{
					case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
					{
						desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
						desc.Texture2D.MipLevels = (UINT)-1;
						desc.Texture2D.MostDetailedMip = 0;
						break;
					}
					case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
					{
						desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
						desc.Texture3D.MipLevels = (UINT)-1;
						desc.Texture3D.MostDetailedMip = 0;
						break;
					}
					case D3D12_RESOURCE_DIMENSION_BUFFER:
					{
						if (info.mStride == 0)
						{
							desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
							desc.RaytracingAccelerationStructure.Location = resource->GetGPUVirtualAddress();
							gAssert(desc.RaytracingAccelerationStructure.Location != NULL);
							resource = nullptr;
						}
						else
						{
							desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
							desc.Buffer.NumElements = (UINT)(resource_desc.Width / info.mStride);
							desc.Buffer.StructureByteStride = info.mStride;
							desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
						}
						break;
					}
					default:
						gAssert(false);
						break;
					}
					gDevice->CreateShaderResourceView(resource, &desc, handle);

					entry_index++;
					handle.ptr += increment_size;
				}
			}
			break;
			case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
			{
				for (UINT j = 0; j < range.NumDescriptors; j++)
				{
					D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
					if (inEntries[entry_index].mResource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
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
						desc.Texture3D.WSize = inEntries[entry_index].mResource->GetDesc().DepthOrArraySize;
					}
					gDevice->CreateUnorderedAccessView(inEntries[entry_index].mResource, nullptr, &desc, handle);

					entry_index++;
					handle.ptr += increment_size;
				}
			}
			break;
			case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
			{
				for (UINT j = 0; j < range.NumDescriptors; j++)
				{
					D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
					desc.SizeInBytes = gAlignUp((UINT)inEntries[entry_index].mResource->GetDesc().Width, (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
					desc.BufferLocation = inEntries[entry_index].mResource->GetGPUVirtualAddress();
					gAssert(desc.BufferLocation != NULL);
					gDevice->CreateConstantBufferView(&desc, handle);

					entry_index++;
					handle.ptr += increment_size;
				}
			}
			break;
			default: gAssert(false); break;
			}
		}
	}
}

void Shader::SetupGraphics()
{
	gCommandList->SetGraphicsRootSignature(mData.mRootSignature.Get());
	ID3D12DescriptorHeap* descriptor_heap = mData.mDescriptorHeap.Get();
	gCommandList->SetDescriptorHeaps(1, &descriptor_heap);
	gCommandList->SetGraphicsRootDescriptorTable(0, mData.mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	gCommandList->SetPipelineState(mData.mPipelineState.Get());
}

void Shader::SetupCompute(ID3D12DescriptorHeap* inHeap, bool inUseTable)
{
	gCommandList->SetComputeRootSignature(mData.mRootSignature.Get());
	ID3D12DescriptorHeap* heap = inHeap != nullptr ? inHeap : mData.mDescriptorHeap.Get();
	gCommandList->SetDescriptorHeaps(1, &heap);
	if (inUseTable)
		gCommandList->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
	gCommandList->SetPipelineState(mData.mPipelineState.Get());
}

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
	auto create_uav = [](ID3D12Resource* inResource, int& outHeapIndex)
	{
		D3D12_RESOURCE_DESC resource_desc = inResource->GetDesc();

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
		D3D12_CPU_DESCRIPTOR_HANDLE handle = gUniversalHeap->GetCPUDescriptorHandleForHeapStart();

		outHeapIndex = gUniversalHeapHandleIndex++;
		handle.ptr += outHeapIndex * gUniversalHeapHandleIncrementSize;
		gDevice->CreateUnorderedAccessView(inResource, nullptr, &desc, handle);
	};

	std::wstring name = gToWString(mName);

	D3D12_RESOURCE_DESC resource_desc = gGetTextureResourceDesc(mWidth, mHeight, mDepth, mFormat);
	D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mResource)));
	mResource->SetName(name.c_str());
	create_uav(mResource.Get(), mResourceHeapIndex);
	
	// SRV for ImGui
	{
		ImGui_ImplDX12_AllocateDescriptor(mGuiCPUHandle, mGuiGPUHandle);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = mFormat;
		srv_desc.ViewDimension = mDepth == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE3D;
		srv_desc.Texture2D.MipLevels = (UINT)-1;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		gDevice->CreateShaderResourceView(mResource.Get(), &srv_desc, mGuiCPUHandle);
	}

	// UIScale
	if (mUIScale == 0.0f)
		mUIScale = 256.0f / static_cast<float>(mWidth);
}

void Texture::Update()
{
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

void ImGuiShowTextures(std::span<Texture> inTextures, const std::string& inName, ImGuiTreeNodeFlags inFlags)
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
			ImGui::Image(reinterpret_cast<ImTextureID>(inTexture.mGuiGPUHandle.ptr), ImVec2(inTexture.mWidth * item_ui_scale, inTexture.mHeight * item_ui_scale), uv0, uv1);
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
			ImGui::Text(text.c_str());
		};

		if (ImGui::BeginPopup("Image Options"))
		{
			if (sTexture != nullptr)
			{
				ImGui::Text(sTexture->mName);

				if (ImGui::Button("Dump"))
					gDumpTexture = sTexture;

				ImGui::Separator();
			}

			ImGui::TextureOption();

			// Extra options
			{
				ImGui::PushItemWidth(100);

				ImGui::SliderFloat("UI Scale", &ui_scale, 1.0, 4.0f);
				ImGui::SameLine();
				ImGui::Checkbox("Flip Y", &flip_y);

				ImGui::PopItemWidth();
			}

			ImGui::EndPopup();
		}

		for (auto&& texture : inTextures)
			add_texture(texture);

		ImGui::TreePop();
	}

	ImGui::End();
}