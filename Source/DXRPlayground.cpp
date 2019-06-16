#include "Thirdparty/imgui/imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "Common.h"

#include "AccelerationStructure.h"
#include "PipelineState.h"
#include "ShaderResource.h"
#include "ShaderTable.h"
#include "RayTrace.h"

#define DX12_ENABLE_DEBUG_LAYER     (1)

static const char*					kApplicationTitle = "DXR Playground";
static const wchar_t*				kApplicationTitleW = L"DXR Playground";

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
FrameContext* WaitForNextFrameResources();
void ResizeSwapChain(HWND hWnd, int width, int height);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void CleanupApplication()
{
	CleanupShaderResource();
	CleanupShaderTable();
	CleanupPipelineState();

	CleanupTopLevelAccelerationStructure();
	CleanupBottomLevelAccelerationStructure();
	CleanupVertexBuffer();
}

void UpdateDrawApplication()
{
	UpdateTopLevelAccelerationStructure();

	uint8_t* pData = nullptr;
	gDxrConstantBufferResource->Map(0, nullptr, (void**)& pData);
	memcpy(pData, &gPerFrameConstantBuffer, sizeof(gPerFrameConstantBuffer));
	gDxrConstantBufferResource->Unmap(0, nullptr);
}

void DrawApplication(ID3D12Resource* frame_render_target_resource)
{
	RayTrace(frame_render_target_resource);
}

void CreateApplication()
{
	CreateVertexBuffer();
	CreateBottomLevelAccelerationStructure();
	CreateTopLevelAccelerationStructure();

	ExecuteAccelerationStructureCreation();

	CreatePipelineState();
	CreateShaderResource();
	CreateShaderTable();
}

// Main code
int main(int, char**)
{
	// Create application window
	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, kApplicationTitleW, nullptr };
	::RegisterClassEx(&wc);
	HWND hwnd = ::CreateWindow(wc.lpszClassName, kApplicationTitleW, WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}
	CreateRenderTarget();

	// Customization - Create
	CreateApplication();

	// Show the window
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(gD3DDevice, NUM_FRAMES_IN_FLIGHT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		gImGuiSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		gImGuiSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	// Main loop
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		// Poll and handle messages (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		if (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			continue;
		}

		// Start the Dear ImGui frame
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Main window
		{
			ImGui::Begin("DXR Playground");
			{
				ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
				ImGui::ColorEdit3("Background Color", (float*)& gPerFrameConstantBuffer.mBackgroundColor[0]);
			}
			ImGui::End();
		}

		// Rendering
		{
			// Frame context
			FrameContext* frameCtxt = WaitForNextFrameResources();
			uint32_t frame_index = gSwapChain->GetCurrentBackBufferIndex();
			ID3D12Resource* frame_render_target_resource = gBackBufferRenderTargetResource[frame_index];
			D3D12_CPU_DESCRIPTOR_HANDLE& frame_render_target_descriptor = gBackBufferRenderTargetDescriptor[frame_index];

			// Frame begin
			{
				frameCtxt->CommandAllocator->Reset();

				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.Transition.pResource = frame_render_target_resource;
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

				gD3DCommandList->Reset(frameCtxt->CommandAllocator, nullptr);
				gD3DCommandList->ResourceBarrier(1, &barrier);
			}

 			// Customization
			UpdateDrawApplication();
			DrawApplication(frame_render_target_resource);

			// Draw ImGui
			{
				gD3DCommandList->OMSetRenderTargets(1, &frame_render_target_descriptor, FALSE, nullptr);
				gD3DCommandList->SetDescriptorHeaps(1, &gImGuiSrvDescHeap);

				ImGui::Render();
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gD3DCommandList);
			}

			// Frame end
			{
				D3D12_RESOURCE_BARRIER barrier = {};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.Transition.pResource = frame_render_target_resource;
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
				gD3DCommandList->ResourceBarrier(1, &barrier);
				gD3DCommandList->Close();
				gD3DCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)& gD3DCommandList);

				gTime += ImGui::GetIO().DeltaTime;
			}			

			// Swap
			{
				gSwapChain->Present(1, 0); // Present with vsync
				//gSwapChain->Present(0, 0); // Present without vsync

				UINT64 fenceValue = gFenceLastSignaledValue + 1;
				gD3DCommandQueue->Signal(gFence, fenceValue);
				gFenceLastSignaledValue = fenceValue;
				frameCtxt->FenceValue = fenceValue;
			}
		}
	}

	// Shutdown
	{
		WaitForLastSubmittedFrame();
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		// Customization - Cleanup
		CleanupApplication();

		CleanupRenderTarget();
		CleanupDeviceD3D();
		::DestroyWindow(hwnd);
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
	}

	return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	{
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = NUM_BACK_BUFFERS;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;	// for IDXGISwapChain2::SetMaximumFrameLatency(), IDXGISwapChain2::GetFrameLatencyWaitableObject()
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Stereo = FALSE;
	}

	if (DX12_ENABLE_DEBUG_LAYER)
	{
		ID3D12Debug* dx12Debug = nullptr;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12Debug))))
		{
			dx12Debug->EnableDebugLayer();
			dx12Debug->Release();
		}
	}

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;
	if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&gD3DDevice)) != S_OK)
		return false;

	// Check DXR
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5;
		memset(&features5, 0, sizeof(features5));
		HRESULT hr = gD3DDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
		if (FAILED(hr) || features5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
		{
			std::cout << "DXR is not supported" << std::endl;
			return false;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = NUM_BACK_BUFFERS;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;
		if (gD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gD3DRtvDescHeap)) != S_OK)
			return false;

		SIZE_T rtvDescriptorSize = gD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = gD3DRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			gBackBufferRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (gD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gImGuiSrvDescHeap)) != S_OK)
			return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		if (gD3DDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&gD3DCommandQueue)) != S_OK)
			return false;
	}

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
	{
		if (gD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gFrameContext[i].CommandAllocator)) != S_OK)
			return false;
	}

	if (gD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gFrameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&gD3DCommandList)) != S_OK)
		return false;

	if (gD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gFence)) != S_OK)
		return false;

	gFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (gFenceEvent == nullptr)
		return false;

	{
		IDXGIFactory4* dxgiFactory = nullptr;
		IDXGISwapChain1* swapChain1 = nullptr;
		if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK ||
			dxgiFactory->CreateSwapChainForHwnd(gD3DCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK ||
			swapChain1->QueryInterface(IID_PPV_ARGS(&gSwapChain)) != S_OK)
			return false;

		// Fullscreen -> Windowed cause crash on resource reference in WM_SIZE handling, disable fullscreen for now
		dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

		swapChain1->Release();
		dxgiFactory->Release();
		gSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		gSwapChainWaitableObject = gSwapChain->GetFrameLatencyWaitableObject();
	}

	return true;
}

void CleanupDeviceD3D()
{
	if (gSwapChain) { gSwapChain->Release(); gSwapChain = nullptr; }
	if (gSwapChainWaitableObject != nullptr) { CloseHandle(gSwapChainWaitableObject); }
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (gFrameContext[i].CommandAllocator) { gFrameContext[i].CommandAllocator->Release(); gFrameContext[i].CommandAllocator = nullptr; }
	if (gD3DCommandQueue) { gD3DCommandQueue->Release(); gD3DCommandQueue = nullptr; }
	if (gD3DCommandList) { gD3DCommandList->Release(); gD3DCommandList = nullptr; }
	if (gD3DRtvDescHeap) { gD3DRtvDescHeap->Release(); gD3DRtvDescHeap = nullptr; }
	if (gImGuiSrvDescHeap) { gImGuiSrvDescHeap->Release(); gImGuiSrvDescHeap = nullptr; }
	if (gFence) { gFence->Release(); gFence = nullptr; }
	if (gFenceEvent) { CloseHandle(gFenceEvent); gFenceEvent = nullptr; }
	if (gD3DDevice) { gD3DDevice->Release(); gD3DDevice = nullptr; }

	if (DX12_ENABLE_DEBUG_LAYER)
	{
		ComPtr<IDXGIDebug1> dxgi_debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
			dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
	}
}

void CreateRenderTarget()
{
	ID3D12Resource* pBackBuffer;
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		gD3DDevice->CreateRenderTargetView(pBackBuffer, nullptr, gBackBufferRenderTargetDescriptor[i]);
		gBackBufferRenderTargetResource[i] = pBackBuffer;
	}
}

void CleanupRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		gSafeRelease(gBackBufferRenderTargetResource[i]);
}

void WaitForLastSubmittedFrame()
{
	FrameContext* frameCtxt = &gFrameContext[gFrameIndex % NUM_FRAMES_IN_FLIGHT];

	UINT64 fenceValue = frameCtxt->FenceValue;
	if (fenceValue == 0)
		return; // No fence was signaled

	frameCtxt->FenceValue = 0;
	if (gFence->GetCompletedValue() >= fenceValue)
		return;

	gFence->SetEventOnCompletion(fenceValue, gFenceEvent);
	WaitForSingleObject(gFenceEvent, INFINITE);
}

FrameContext * WaitForNextFrameResources()
{
	UINT nextFrameIndex = gFrameIndex + 1;
	gFrameIndex = nextFrameIndex;

	HANDLE waitableObjects[] = { gSwapChainWaitableObject, nullptr };
	DWORD numWaitableObjects = 1;

	FrameContext* frameCtxt = &gFrameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
	UINT64 fenceValue = frameCtxt->FenceValue;
	if (fenceValue != 0) // means no fence was signaled
	{
		frameCtxt->FenceValue = 0;
		gFence->SetEventOnCompletion(fenceValue, gFenceEvent);
		waitableObjects[1] = gFenceEvent;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

	return frameCtxt;
}

void ResizeSwapChain(HWND hWnd, int width, int height)
{
	DXGI_SWAP_CHAIN_DESC1 sd;
	gSwapChain->GetDesc1(&sd);
	sd.Width = width;
	sd.Height = height;

	IDXGIFactory4* dxgiFactory = nullptr;
	gSwapChain->GetParent(IID_PPV_ARGS(&dxgiFactory));

	gSwapChain->Release();
	CloseHandle(gSwapChainWaitableObject);

	IDXGISwapChain1* swapChain1 = nullptr;
	dxgiFactory->CreateSwapChainForHwnd(gD3DCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1);
	swapChain1->QueryInterface(IID_PPV_ARGS(&gSwapChain));
	swapChain1->Release();
	dxgiFactory->Release();

	gSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);

	gSwapChainWaitableObject = gSwapChain->GetFrameLatencyWaitableObject();
	assert(gSwapChainWaitableObject != nullptr);
}

// Win32 message handler
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (gD3DDevice != nullptr && wParam != SIZE_MINIMIZED)
		{
			WaitForLastSubmittedFrame();

			// Recreate window size dependent resources
			CleanupShaderResource();	
			CleanupRenderTarget();
			// ImGui sample re-create swap chain and stop DXGI_MWA_NO_ALT_ENTER from working
			gSwapChain->ResizeBuffers(
				NUM_BACK_BUFFERS, 
				gMax((UINT)LOWORD(lParam), 8u), 
				gMax((UINT)HIWORD(lParam), 8u), 
				DXGI_FORMAT_R8G8B8A8_UNORM, 
				DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
			CreateRenderTarget();	
			CreateShaderResource();
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
