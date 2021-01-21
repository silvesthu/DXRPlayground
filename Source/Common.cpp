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
glm::float32						gTime = 0.0f;
ShaderType::PerFrame				gPerFrameConstantBuffer = {};

void Shader::Initialize(const std::vector<Shader::DescriptorEntry>& inEntries)
{
	// Check if root signature is supported
	const D3D12_ROOT_SIGNATURE_DESC* root_signature_desc = mRootSignatureDeserializer->GetRootSignatureDesc();

	assert(root_signature_desc->NumParameters == 1);
	assert(root_signature_desc->pParameters[0].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
	const D3D12_ROOT_DESCRIPTOR_TABLE& table = root_signature_desc->pParameters[0].DescriptorTable;

	// DescriptorHeap
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = table.NumDescriptorRanges;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		gValidate(gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mDescriptorHeap)));
	}

	// DescriptorTable
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		UINT increment_size = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		assert(inEntries.size() == table.NumDescriptorRanges);

		for (UINT i = 0; i < table.NumDescriptorRanges; i++)
		{
			const D3D12_DESCRIPTOR_RANGE& range = table.pDescriptorRanges[i];
			switch (range.RangeType)
			{
			case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MipLevels = 1;
				desc.Texture2D.MostDetailedMip = 0;
				desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				gDevice->CreateShaderResourceView(inEntries[i].mResource, &desc, handle);
			}
			break;
			case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
				desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				gDevice->CreateUnorderedAccessView(inEntries[i].mResource, nullptr, &desc, handle);
			}
			break;
			case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
			{
				D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
				desc.BufferLocation = inEntries[i].mAddress;
				desc.SizeInBytes = gAlignUp((UINT)sizeof(ShaderType::Atmosphere), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
				gDevice->CreateConstantBufferView(&desc, handle);
			}
			break;
			default: assert(false); break;
			}

			handle.ptr += increment_size;
		}
	}
}

void Shader::Setup()
{
	gCommandList->SetComputeRootSignature(mRootSignature.Get());
	ID3D12DescriptorHeap* descriptor_heap = mDescriptorHeap.Get();
	gCommandList->SetDescriptorHeaps(1, &descriptor_heap);
	gCommandList->SetComputeRootDescriptorTable(0, mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	gCommandList->SetPipelineState(mPipelineState.Get());
}

void Texture::Initialize()
{
	D3D12_RESOURCE_DESC resource_desc = gGetTextureResourceDesc(mWidth, mHeight, mFormat);
	D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

	gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mResource)));
	mResource->SetName(L"Atmosphere.Transmittance");

	// SRV for ImGui
	ImGui_ImplDX12_AllocateDescriptor(mCPUHandle, mGPUHandle);
	D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
	srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv_desc.Texture2D.MipLevels = (UINT)-1;
	srv_desc.Texture2D.MostDetailedMip = 0;
	srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	gDevice->CreateShaderResourceView(mResource.Get(), &srv_desc, mCPUHandle);
}
