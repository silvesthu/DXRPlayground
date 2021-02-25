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

Shader								gCompositeShader = Shader().VSName(L"ScreenspaceTriangleVS").PSName(L"CompositePS");

// Frame
FrameContext						gFrameContext[NUM_FRAMES_IN_FLIGHT] = {};
glm::uint32							gFrameIndex = 0;
ShaderType::PerFrame				gPerFrameConstantBuffer = {};

// Capture
Texture*							gDumpTexture = nullptr;
Texture								gDumpTextureProxy = {};

void Shader::InitializeDescriptors(const std::vector<ID3D12Resource*>& inEntries)
{
	// Check if root signature is supported
	const D3D12_ROOT_SIGNATURE_DESC* root_signature_desc = mData.mRootSignatureDeserializer->GetRootSignatureDesc();

	assert(root_signature_desc->NumParameters >= 1);
	assert(root_signature_desc->pParameters[0].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
	const D3D12_ROOT_DESCRIPTOR_TABLE& table = root_signature_desc->pParameters[0].DescriptorTable;

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
	{
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
					D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
					desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					if (inEntries[entry_index]->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
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
					gDevice->CreateShaderResourceView(inEntries[entry_index], &desc, handle);

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
					if (inEntries[entry_index]->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
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
						desc.Texture3D.WSize = inEntries[entry_index]->GetDesc().DepthOrArraySize;
					}
					gDevice->CreateUnorderedAccessView(inEntries[entry_index], nullptr, &desc, handle);

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
					desc.SizeInBytes = (UINT)inEntries[entry_index]->GetDesc().Width;
					desc.BufferLocation = inEntries[entry_index]->GetGPUVirtualAddress();
					gDevice->CreateConstantBufferView(&desc, handle);

					entry_index++;
					handle.ptr += increment_size;
				}
			}
			break;
			default: assert(false); break;
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

void Shader::SetupCompute()
{
	gCommandList->SetComputeRootSignature(mData.mRootSignature.Get());
	ID3D12DescriptorHeap* descriptor_heap = mData.mDescriptorHeap.Get();
	gCommandList->SetDescriptorHeaps(1, &descriptor_heap);
	gCommandList->SetComputeRootDescriptorTable(0, mData.mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	gCommandList->SetPipelineState(mData.mPipelineState.Get());
}

void Texture::Initialize()
{
	D3D12_RESOURCE_DESC resource_desc = gGetTextureResourceDesc(mWidth, mHeight, mDepth, mFormat);
	D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&mResource)));
	mResource->SetName(gToWString(mName).c_str());
	
	// SRV for ImGui
	ImGui_ImplDX12_AllocateDescriptor(mCPUHandle, mGPUHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
	srv_desc.Format = mFormat;
	srv_desc.ViewDimension = mDepth == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE3D;
	srv_desc.Texture2D.MipLevels = (UINT)-1;
	srv_desc.Texture2D.MostDetailedMip = 0;
	srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	gDevice->CreateShaderResourceView(mResource.Get(), &srv_desc, mCPUHandle);
}
