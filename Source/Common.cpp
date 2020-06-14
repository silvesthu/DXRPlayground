#include "Common.h"

// System
ID3D12Device5*						gDevice = nullptr;
ID3D12DescriptorHeap*				gRtvDescHeap = nullptr;
ID3D12CommandQueue*					gCommandQueue = nullptr;
ID3D12GraphicsCommandList4*			gCommandList = nullptr;

ID3D12Fence*						gIncrementalFence = nullptr;
HANDLE                       		gIncrementalFenceEvent = nullptr;
UINT64                       		gFenceLastSignaledValue = 0;

IDXGISwapChain3*					gSwapChain = nullptr;
HANDLE                       		gSwapChainWaitableObject = nullptr;
ID3D12Resource*						gBackBufferRenderTargetResource[NUM_BACK_BUFFERS] = {};
D3D12_CPU_DESCRIPTOR_HANDLE			gBackBufferRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

ID3D12DescriptorHeap*				gImGuiSrvDescHeap = nullptr;

// Application
VertexBuffer 						gDxrTriangleVertexBuffer;
VertexBuffer						gDxrPlaneVertexBuffer;
BottomLevelAccelerationStructure 	gDxrTriangleBLAS;
BottomLevelAccelerationStructure 	gDxrPlaneBLAS;
ID3D12Resource*						gDxrTopLevelAccelerationStructureScratch = nullptr;
ID3D12Resource*						gDxrTopLevelAccelerationStructureDest = nullptr;
ID3D12Resource*						gDxrTopLevelAccelerationStructureInstanceDesc = nullptr;

ID3D12RootSignature*				gDxrEmptyRootSignature = nullptr;
ID3D12StateObject*					gDxrStateObject = nullptr;
ShaderTable							gDxrShaderTable = {};
ID3D12Resource*						gDxrOutputResource = nullptr;
ID3D12DescriptorHeap*				gDxrCbvSrvUavHeap = nullptr;
ID3D12Resource*						gDxrHitConstantBufferResource = nullptr;

ID3D12Resource*						gConstantGPUBuffer = nullptr;

// Frame
FrameContext                		gFrameContext[NUM_FRAMES_IN_FLIGHT] = {};
UINT                    			gFrameIndex = 0;
float								gTime = 0.0f;

PerFrame							gPerFrameConstantBuffer = {};