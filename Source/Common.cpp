#include "Common.h"

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

SystemShader						gCompositeShader;

// Frame
FrameContext                		gFrameContext[NUM_FRAMES_IN_FLIGHT] = {};
glm::uint32                			gFrameIndex = 0;
glm::float32						gTime = 0.0f;
ShaderType::PerFrame				gPerFrameConstantBuffer = {};