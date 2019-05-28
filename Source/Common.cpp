#include "Common.h"

// Data
FrameContext                		gFrameContext[NUM_FRAMES_IN_FLIGHT] = {};
UINT                    			gFrameIndex = 0;

ID3D12Device5*						gD3DDevice = nullptr;
ID3D12DescriptorHeap*				gD3DRtvDescHeap = nullptr;
ID3D12CommandQueue*					gD3DCommandQueue = nullptr;
ID3D12GraphicsCommandList4*			gD3DCommandList = nullptr;
ID3D12Fence*						gFence = nullptr;
HANDLE                       		gFenceEvent = nullptr;
UINT64                       		gFenceLastSignaledValue = 0;
IDXGISwapChain3*					gSwapChain = nullptr;
HANDLE                       		gSwapChainWaitableObject = nullptr;
ID3D12Resource*						gBackBufferRenderTargetResource[NUM_BACK_BUFFERS] = {};
D3D12_CPU_DESCRIPTOR_HANDLE			gBackBufferRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

// ImGui - Data
ID3D12DescriptorHeap*				gImGuiSrvDescHeap = nullptr;

// Customization - Data
ID3D12Resource*						gDxrVertexBuffer = nullptr;
ID3D12Resource*						gDxrBottomLevelAccelerationStructureScratch = nullptr;
ID3D12Resource*						gDxrBottomLevelAccelerationStructureDest = nullptr;
ID3D12Resource*						gDxrTopLevelAccelerationStructureScratch = nullptr;
ID3D12Resource*						gDxrTopLevelAccelerationStructureDest = nullptr;
ID3D12Resource*						gDxrTopLevelAccelerationStructureInstanceDesc = nullptr;

ID3D12RootSignature*				gDxrEmptyRootSignature = nullptr;
ID3D12StateObject*					gDxrStateObject = nullptr;
ID3D12Resource*						gDxrShaderTable = nullptr;
uint64_t							gDxrShaderTableEntrySize = 0;
ID3D12Resource*						gDxrOutputResource = nullptr;
ID3D12Resource*						gDxrConstantBufferResource = nullptr;
ID3D12DescriptorHeap*				gDxrCbvSrvUavHeap = nullptr;
ID3D12Resource*						gDxrHitConstantBufferResource = nullptr;

PerFrame							gPerFrameConstantBuffer = {};