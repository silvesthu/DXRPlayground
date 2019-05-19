#include "Thirdparty/imgui/imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxcapi.h>
#include <dxgidebug.h>
#include <comdef.h>
#include <wrl.h>	// For ComPtr. See https://github.com/Microsoft/DirectXTK/wiki/ComPtr
using Microsoft::WRL::ComPtr;

#include <tchar.h>
#include <string>
#include <iostream>
#include <array>
#include <vector>

#define DX12_ENABLE_DEBUG_LAYER     1

struct FrameContext
{
	ID3D12CommandAllocator* CommandAllocator;
	UINT64                  FenceValue;
};

// Data
static int const                    NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext                 g_FrameContext[NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_FrameIndex = 0;

static int const                    NUM_BACK_BUFFERS = 3;
static ID3D12Device5*				g_D3DDevice = NULL;
static ID3D12DescriptorHeap*		g_D3DRtvDescHeap = NULL;
static ID3D12DescriptorHeap*		g_D3DSrvDescHeap = NULL;
static ID3D12CommandQueue*			g_D3DCommandQueue = NULL;
static ID3D12GraphicsCommandList4*	g_D3DCommandList = NULL;
static ID3D12Fence*					g_Fence = NULL;
static HANDLE                       g_FenceEvent = NULL;
static UINT64                       g_FenceLastSignaledValue = 0;
static IDXGISwapChain3*				g_SwapChain = NULL;
static HANDLE                       g_SwapChainWaitableObject = NULL;
static ID3D12Resource*				g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

// Customization - Data
static const char*					g_ApplicationTitle = "DXR Playground";
static const wchar_t*				g_ApplicationTitleW = L"DXR Playground";

static uint64_t						g_FenceValue = 0;

static ID3D12Resource*				g_VertexBuffer = nullptr;
static ID3D12Resource*				g_BottomLevelAccelerationStructureScratch = nullptr;
static ID3D12Resource*				g_BottomLevelAccelerationStructureDest = nullptr;
static ID3D12Resource*				g_TopLevelAccelerationStructureScratch = nullptr;
static ID3D12Resource*				g_TopLevelAccelerationStructureDest = nullptr;
static ID3D12Resource*				g_TopLevelAccelerationStructureInstanceDesc = nullptr;

static ID3D12StateObject*			g_StateObject = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
FrameContext* WaitForNextFrameResources();
void ResizeSwapChain(HWND hWnd, int width, int height);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Customization - Utility
template <typename T>
void gSafeRelease(T*& pointer)
{
	if (pointer != nullptr)
	{
		pointer->Release();
		pointer = nullptr;
	}
}

// Customization - Forward declarations
void CreateVertexBuffer();
void CleanupVertexBuffer();
void CreateBottomLevelAccelerationStructure();
void CleanupBottomLevelAccelerationStructure();
void CreateTopLevelAccelerationStructure();
void CleanupTopLevelAccelerationStructure();
void ExecuteAccelerationStructureCreation();
void CreatePipelineState();

// Main code
int main(int, char**)
{
	// Create application window
	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, g_ApplicationTitleW, NULL };
	::RegisterClassEx(&wc);
	HWND hwnd = ::CreateWindow(wc.lpszClassName, g_ApplicationTitleW, WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// Customization - Initialize
	{
		// Create resource
		CreateVertexBuffer();
		CreateBottomLevelAccelerationStructure();
		CreateTopLevelAccelerationStructure();

		// The tutorial doesn't have any resource lifetime management, so we flush and sync here. 
		// This is not required by the DXR spec - you can submit the list whenever you like as long as you take care of the resources lifetime.
		ExecuteAccelerationStructureCreation();

		CreatePipelineState();
	}

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
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(g_D3DDevice, NUM_FRAMES_IN_FLIGHT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		g_D3DSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		g_D3DSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'misc/fonts/README.txt' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);

	// Our state
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

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
		if (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			continue;
		}

		// Start the Dear ImGui frame
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// ImGui sample code
		{
			// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
			if (show_demo_window)
				ImGui::ShowDemoWindow(&show_demo_window);

			// 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
			{
				static float f = 0.0f;
				static int counter = 0;

				ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

				ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
				ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
				ImGui::Checkbox("Another Window", &show_another_window);

				ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
				ImGui::ColorEdit3("clear color", (float*)& clear_color); // Edit 3 floats representing a color

				if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
					counter++;
				ImGui::SameLine();
				ImGui::Text("counter = %d", counter);

				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
				ImGui::End();
			}

			// 3. Show another simple window.
			if (show_another_window)
			{
				ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
				ImGui::Text("Hello from another window!");
				if (ImGui::Button("Close Me"))
					show_another_window = false;
				ImGui::End();
			}
		}

		// Customization - GUI
		{
			// Window Title
			{
				char buff[256];
				snprintf(buff, sizeof(buff), "%s - Average %.3f ms/frame (%.1f FPS)", g_ApplicationTitle, 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
				SetWindowTextA(hwnd, buff);
			}
		}

		// Rendering
		{
			// Wait
			FrameContext* frameCtxt = WaitForNextFrameResources();

			// Reset
			frameCtxt->CommandAllocator->Reset();

			// Frame begin
			D3D12_RESOURCE_BARRIER barrier = {};
			{
				UINT backBufferIdx = g_SwapChain->GetCurrentBackBufferIndex();

				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

				g_D3DCommandList->Reset(frameCtxt->CommandAllocator, NULL);
				g_D3DCommandList->ResourceBarrier(1, &barrier);
				g_D3DCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], (float*)& clear_color, 0, NULL);
				g_D3DCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, NULL);
				g_D3DCommandList->SetDescriptorHeaps(1, &g_D3DSrvDescHeap);
			}

			// Customization - Draw
			{
				// Draw screen quad
				{

				}
			}

			// Draw ImGui
			{
				// Record
				ImGui::Render();
				// Draw (Create command)
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_D3DCommandList);
			}

			// Frame end
			{
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
				g_D3DCommandList->ResourceBarrier(1, &barrier);
				g_D3DCommandList->Close();
				g_D3DCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)& g_D3DCommandList);
			}			

			// Swap
			{
				g_SwapChain->Present(1, 0); // Present with vsync
				//g_SwapChain->Present(0, 0); // Present without vsync

				UINT64 fenceValue = g_FenceLastSignaledValue + 1;
				g_D3DCommandQueue->Signal(g_Fence, fenceValue);
				g_FenceLastSignaledValue = fenceValue;
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
		CleanupTopLevelAccelerationStructure();
		CleanupBottomLevelAccelerationStructure();
		CleanupVertexBuffer();

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
		ID3D12Debug* dx12Debug = NULL;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12Debug))))
		{
			dx12Debug->EnableDebugLayer();
			dx12Debug->Release();
		}
	}

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;
	if (D3D12CreateDevice(NULL, featureLevel, IID_PPV_ARGS(&g_D3DDevice)) != S_OK)
		return false;

	// Check DXR
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5;
		memset(&features5, 0, sizeof(features5));
		HRESULT hr = g_D3DDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
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
		if (g_D3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_D3DRtvDescHeap)) != S_OK)
			return false;

		SIZE_T rtvDescriptorSize = g_D3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_D3DRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			g_mainRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (g_D3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_D3DSrvDescHeap)) != S_OK)
			return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		if (g_D3DDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_D3DCommandQueue)) != S_OK)
			return false;
	}

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
	{
		if (g_D3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_FrameContext[i].CommandAllocator)) != S_OK)
			return false;
	}

	if (g_D3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_FrameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&g_D3DCommandList)) != S_OK)
		return false;

	if (g_D3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_Fence)) != S_OK)
		return false;

	g_FenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_FenceEvent == NULL)
		return false;

	{
		IDXGIFactory4* dxgiFactory = NULL;
		IDXGISwapChain1* swapChain1 = NULL;
		if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK ||
			dxgiFactory->CreateSwapChainForHwnd(g_D3DCommandQueue, hWnd, &sd, NULL, NULL, &swapChain1) != S_OK ||
			swapChain1->QueryInterface(IID_PPV_ARGS(&g_SwapChain)) != S_OK)
			return false;
		swapChain1->Release();
		dxgiFactory->Release();
		g_SwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		g_SwapChainWaitableObject = g_SwapChain->GetFrameLatencyWaitableObject();
	}

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_SwapChain) { g_SwapChain->Release(); g_SwapChain = NULL; }
	if (g_SwapChainWaitableObject != NULL) { CloseHandle(g_SwapChainWaitableObject); }
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (g_FrameContext[i].CommandAllocator) { g_FrameContext[i].CommandAllocator->Release(); g_FrameContext[i].CommandAllocator = NULL; }
	if (g_D3DCommandQueue) { g_D3DCommandQueue->Release(); g_D3DCommandQueue = NULL; }
	if (g_D3DCommandList) { g_D3DCommandList->Release(); g_D3DCommandList = NULL; }
	if (g_D3DRtvDescHeap) { g_D3DRtvDescHeap->Release(); g_D3DRtvDescHeap = NULL; }
	if (g_D3DSrvDescHeap) { g_D3DSrvDescHeap->Release(); g_D3DSrvDescHeap = NULL; }
	if (g_Fence) { g_Fence->Release(); g_Fence = NULL; }
	if (g_FenceEvent) { CloseHandle(g_FenceEvent); g_FenceEvent = NULL; }
	if (g_D3DDevice) { g_D3DDevice->Release(); g_D3DDevice = NULL; }

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
		g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		g_D3DDevice->CreateRenderTargetView(pBackBuffer, NULL, g_mainRenderTargetDescriptor[i]);
		g_mainRenderTargetResource[i] = pBackBuffer;
	}
}

void CleanupRenderTarget()
{
	WaitForLastSubmittedFrame();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = NULL; }
}

void WaitForLastSubmittedFrame()
{
	FrameContext* frameCtxt = &g_FrameContext[g_FrameIndex % NUM_FRAMES_IN_FLIGHT];

	UINT64 fenceValue = frameCtxt->FenceValue;
	if (fenceValue == 0)
		return; // No fence was signaled

	frameCtxt->FenceValue = 0;
	if (g_Fence->GetCompletedValue() >= fenceValue)
		return;

	g_Fence->SetEventOnCompletion(fenceValue, g_FenceEvent);
	WaitForSingleObject(g_FenceEvent, INFINITE);
}

FrameContext * WaitForNextFrameResources()
{
	UINT nextFrameIndex = g_FrameIndex + 1;
	g_FrameIndex = nextFrameIndex;

	HANDLE waitableObjects[] = { g_SwapChainWaitableObject, NULL };
	DWORD numWaitableObjects = 1;

	FrameContext* frameCtxt = &g_FrameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
	UINT64 fenceValue = frameCtxt->FenceValue;
	if (fenceValue != 0) // means no fence was signaled
	{
		frameCtxt->FenceValue = 0;
		g_Fence->SetEventOnCompletion(fenceValue, g_FenceEvent);
		waitableObjects[1] = g_FenceEvent;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

	return frameCtxt;
}

void ResizeSwapChain(HWND hWnd, int width, int height)
{
	DXGI_SWAP_CHAIN_DESC1 sd;
	g_SwapChain->GetDesc1(&sd);
	sd.Width = width;
	sd.Height = height;

	IDXGIFactory4* dxgiFactory = NULL;
	g_SwapChain->GetParent(IID_PPV_ARGS(&dxgiFactory));

	g_SwapChain->Release();
	CloseHandle(g_SwapChainWaitableObject);

	IDXGISwapChain1* swapChain1 = NULL;
	dxgiFactory->CreateSwapChainForHwnd(g_D3DCommandQueue, hWnd, &sd, NULL, NULL, &swapChain1);
	swapChain1->QueryInterface(IID_PPV_ARGS(&g_SwapChain));
	swapChain1->Release();
	dxgiFactory->Release();

	g_SwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);

	g_SwapChainWaitableObject = g_SwapChain->GetFrameLatencyWaitableObject();
	assert(g_SwapChainWaitableObject != NULL);
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
		if (g_D3DDevice != NULL && wParam != SIZE_MINIMIZED)
		{
			ImGui_ImplDX12_InvalidateDeviceObjects();
			CleanupRenderTarget();
			ResizeSwapChain(hWnd, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
			CreateRenderTarget();
			ImGui_ImplDX12_CreateDeviceObjects();
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

void CreateVertexBuffer()
{
	std::array<float, 9> vertices =
	{
		0.0f,    1.0f,  0.0f,
		0.866f,  -0.5f, 0.0f,
		-0.866f, -0.5f, 0.0f,
	};

	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Alignment = 0;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	bufDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufDesc.Height = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.SampleDesc.Quality = 0;
	bufDesc.Width = sizeof(vertices);

	D3D12_HEAP_PROPERTIES props;
	memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
	props.Type = D3D12_HEAP_TYPE_UPLOAD;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_VertexBuffer));

	uint8_t* pData = nullptr;
	g_VertexBuffer->Map(0, nullptr, (void**)& pData);
	memcpy(pData, vertices.data(), sizeof(vertices));
	g_VertexBuffer->Unmap(0, nullptr);
}

void CleanupVertexBuffer()
{
	if (g_VertexBuffer) { g_VertexBuffer->Release(); g_VertexBuffer = NULL; }
}

void CreateBottomLevelAccelerationStructure()
{
	D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
	geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geomDesc.Triangles.VertexBuffer.StartAddress = g_VertexBuffer->GetGPUVirtualAddress();
	geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
	geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geomDesc.Triangles.VertexCount = 3;
	geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	// Get buffer sizes
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = 1;
	inputs.pGeometryDescs = &geomDesc;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;

	// Create the buffers. They need to support UAV, and since we are going to immediately use them, we create them with an unordered-access state
	{
		D3D12_HEAP_PROPERTIES props = {};
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		g_D3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Alignment = 0;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.Height = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.MipLevels = 1;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.SampleDesc.Quality = 0;
		bufDesc.Width = info.ScratchDataSizeInBytes;

		g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&g_BottomLevelAccelerationStructureScratch));

		bufDesc.Width = info.ResultDataMaxSizeInBytes;
		g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&g_BottomLevelAccelerationStructureDest));
	}

	// Create the bottom level acceleration structure
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
		asDesc.Inputs = inputs;
		asDesc.DestAccelerationStructureData = g_BottomLevelAccelerationStructureDest->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = g_BottomLevelAccelerationStructureScratch->GetGPUVirtualAddress();

		g_D3DCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	}

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = g_BottomLevelAccelerationStructureDest;
		g_D3DCommandList->ResourceBarrier(1, &uavBarrier);
	}
}

void CleanupBottomLevelAccelerationStructure()
{
	gSafeRelease(g_BottomLevelAccelerationStructureScratch);
	gSafeRelease(g_BottomLevelAccelerationStructureDest);
}

void CreateTopLevelAccelerationStructure()
{
	// Get buffer sizes
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
	inputs.NumDescs = 1;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	// Create the buffers
	{
		D3D12_HEAP_PROPERTIES props = {};
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
		g_D3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Alignment = 0;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.Height = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.MipLevels = 1;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.SampleDesc.Quality = 0;
		bufDesc.Width = info.ScratchDataSizeInBytes;

		g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&g_TopLevelAccelerationStructureScratch));

		bufDesc.Width = info.ResultDataMaxSizeInBytes;
		g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&g_TopLevelAccelerationStructureDest));

		props.Type = D3D12_HEAP_TYPE_UPLOAD;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		bufDesc.Width = sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
		g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_TopLevelAccelerationStructureInstanceDesc));

		// The instance desc should be inside a buffer, create and map the buffer
		D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc;
		g_TopLevelAccelerationStructureInstanceDesc->Map(0, nullptr, (void**)& pInstanceDesc);

		// Initialize the instance desc. We only have a single instance
		pInstanceDesc->InstanceID = 0;                            // This value will be exposed to the shader via InstanceID()
		pInstanceDesc->InstanceContributionToHitGroupIndex = 0;   // This is the offset inside the shader-table. We only have a single geometry, so the offset 0
		pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		float m[] =
		{
			1,0,0,0,
			0,1,0,0,
			0,0,1,0,
			0,0,0,1,
		}; // Identity matrix
		memcpy(pInstanceDesc->Transform, &m, sizeof(pInstanceDesc->Transform));
		pInstanceDesc->AccelerationStructure = g_BottomLevelAccelerationStructureDest->GetGPUVirtualAddress();
		pInstanceDesc->InstanceMask = 0xFF;

		// Unmap
		g_TopLevelAccelerationStructureInstanceDesc->Unmap(0, nullptr);
	}

	// Create the top level acceleration structure
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
		asDesc.Inputs = inputs;
		asDesc.Inputs.InstanceDescs = g_TopLevelAccelerationStructureInstanceDesc->GetGPUVirtualAddress();
		asDesc.DestAccelerationStructureData = g_TopLevelAccelerationStructureDest->GetGPUVirtualAddress();
		asDesc.ScratchAccelerationStructureData = g_TopLevelAccelerationStructureScratch->GetGPUVirtualAddress();

		g_D3DCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	}

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = g_TopLevelAccelerationStructureDest;
		g_D3DCommandList->ResourceBarrier(1, &uavBarrier);
	}
}

void CleanupTopLevelAccelerationStructure()
{
	gSafeRelease(g_TopLevelAccelerationStructureScratch);
	gSafeRelease(g_TopLevelAccelerationStructureDest);
	gSafeRelease(g_TopLevelAccelerationStructureInstanceDesc);
}

void ExecuteAccelerationStructureCreation()
{
	g_D3DCommandList->Close();
	g_D3DCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList * const*)& g_D3DCommandList);
	g_FenceValue++;
	g_D3DCommandQueue->Signal(g_Fence, g_FenceValue);
	g_Fence->SetEventOnCompletion(g_FenceValue, g_FenceEvent);
	WaitForSingleObject(g_FenceEvent, INFINITE);

	// CommandList is closed
}

const char gShaderSource[] = R"(
RaytracingAccelerationStructure gRtScene : register(t0);
RWTexture2D<float4> gOutput : register(u0);

float3 linearToSrgb(float3 c)
{
    // Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
    return srgb;
}

[shader("raygeneration")]
void rayGen()
{  
    uint3 launchIndex = DispatchRaysIndex();
    float3 col = linearToSrgb(float3(0.4, 0.6, 0.2));
    gOutput[launchIndex.xy] = float4(col, 1);
}

struct Payload
{
    bool hit;
};

[shader("miss")]
void miss(inout Payload payload)
{
    payload.hit = false;
}

[shader("closesthit")]
void chs(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hit = true;
}
)";

IDxcBlob* CompileShader(const wchar_t* inName, const char* inSource, uint32_t inSize)
{
	static DxcCreateInstanceProc sDxcCreateInstanceProc = nullptr;

	if (sDxcCreateInstanceProc == nullptr)
	{
		HMODULE dll = LoadLibraryW(L"dxcompiler.dll");
		assert(dll != nullptr);
		sDxcCreateInstanceProc = (DxcCreateInstanceProc)GetProcAddress(dll, "DxcCreateInstance");
	}

	IDxcLibrary* library;
	IDxcBlobEncoding* blob_encoding;
	sDxcCreateInstanceProc(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void**)& library);
	if (FAILED(library->CreateBlobWithEncodingFromPinned(inSource, inSize, CP_UTF8, &blob_encoding)))
	{
		assert(false);
		return nullptr;
	}

	IDxcCompiler* compiler;
	sDxcCreateInstanceProc(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void**)& compiler);

	IDxcOperationResult* operation_result;
	assert(SUCCEEDED(compiler->Compile(
		blob_encoding,		// program text
		inName,				// file name, mostly for error messages
		L"",				// entry point function
		L"lib_6_3",			// target profile
		nullptr, 0,			// compilation arguments and their count
		nullptr, 0,			// name/value defines and their count
		nullptr,			// handler for #include directives
		&operation_result)));

	HRESULT compile_result;
	assert(SUCCEEDED(operation_result->GetStatus(&compile_result)));

	if (FAILED(compile_result)) 
	{
		IDxcBlobEncoding* pPrintBlob, *pPrintBlob16;
		assert(SUCCEEDED(operation_result->GetErrorBuffer(&pPrintBlob)));
		// We can use the library to get our preferred encoding.
		assert(SUCCEEDED(library->GetBlobAsUtf16(pPrintBlob, &pPrintBlob16)));
		wprintf(L"%*s", (int)pPrintBlob16->GetBufferSize() / 2, (LPCWSTR)pPrintBlob16->GetBufferPointer());
		pPrintBlob->Release();
		pPrintBlob16->Release();
		return nullptr;
	}

	IDxcBlob* blob = nullptr;
	assert(SUCCEEDED(operation_result->GetResult(&blob)));
	return blob;
}

struct RootSignatureDescriptor
{
	D3D12_ROOT_SIGNATURE_DESC mDesc = {};
	std::vector<D3D12_DESCRIPTOR_RANGE> mDescriptorRanges;
	std::vector<D3D12_ROOT_PARAMETER> mRootParameters;
};

void GenerateRayGenLocalRootDesc(RootSignatureDescriptor& outDesc)
{
	// gOutput
	D3D12_DESCRIPTOR_RANGE descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 0;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// gRtScene
	descriptor_range = {};
	descriptor_range.BaseShaderRegister = 0;
	descriptor_range.NumDescriptors = 1;
	descriptor_range.RegisterSpace = 0;
	descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptor_range.OffsetInDescriptorsFromTableStart = 1;
	outDesc.mDescriptorRanges.push_back(descriptor_range);

	// Root Parameters
	D3D12_ROOT_PARAMETER root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)outDesc.mDescriptorRanges.size();
	root_parameter.DescriptorTable.pDescriptorRanges = outDesc.mDescriptorRanges.data();
	outDesc.mRootParameters.push_back(root_parameter);

	// Create the desc
	outDesc.mDesc.NumParameters = (UINT)outDesc.mRootParameters.size();
	outDesc.mDesc.pParameters = outDesc.mRootParameters.data();
	outDesc.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
}

ComPtr<ID3D12RootSignature> CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc)
{
	ComPtr<ID3DBlob> signature_blob;
	ComPtr<ID3DBlob> error_blob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
	if (FAILED(hr))
	{
		std::string str((char*)error_blob->GetBufferPointer(), error_blob->GetBufferSize());
		OutputDebugStringA(str.c_str());
		assert(false);
		return nullptr;
	}
	ComPtr<ID3D12RootSignature> root_signature;
	assert(SUCCEEDED(g_D3DDevice->CreateRootSignature(0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature))));
	return root_signature;
}

// Hold data for D3D12_STATE_SUBOBJECT
template <typename DescType, D3D12_STATE_SUBOBJECT_TYPE SubObjecType>
struct StateSubobjectHolder
{
	StateSubobjectHolder()
	{
		mStateSubobject.Type = SubObjecType;
		mStateSubobject.pDesc = &mDesc;
	}

	D3D12_STATE_SUBOBJECT mStateSubobject = {};

protected:
	DescType mDesc = {};
};

// Shader binary
struct DXILLibrary : public StateSubobjectHolder<D3D12_DXIL_LIBRARY_DESC, D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY>
{
	DXILLibrary(IDxcBlob* inShaderBlob, const wchar_t* inEntryPoint[], uint32_t inEntryPointCount) : mShaderBlob(inShaderBlob)
	{
		mExportDescs.resize(inEntryPointCount);
		mExportNames.resize(inEntryPointCount);

		if (inShaderBlob)
		{
			mDesc.DXILLibrary.pShaderBytecode = inShaderBlob->GetBufferPointer();
			mDesc.DXILLibrary.BytecodeLength = inShaderBlob->GetBufferSize();
			mDesc.NumExports = inEntryPointCount;
			mDesc.pExports = mExportDescs.data();

			for (uint32_t i = 0; i < inEntryPointCount; i++)
			{
				mExportNames[i] = inEntryPoint[i];
				mExportDescs[i].Name = mExportNames[i].c_str();
				mExportDescs[i].Flags = D3D12_EXPORT_FLAG_NONE;
				mExportDescs[i].ExportToRename = nullptr;
			}
		}
	}

private:
	ComPtr<IDxcBlob> mShaderBlob;
	std::vector<D3D12_EXPORT_DESC> mExportDescs;
	std::vector<std::wstring> mExportNames;
};

// Ray tracing shader structure
struct HitGroup : public StateSubobjectHolder<D3D12_HIT_GROUP_DESC, D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP>
{
	HitGroup(const wchar_t* inAnyHitShaderImport, const wchar_t* inClosestHitShaderImport, const wchar_t* inHitGroupExport)
	{
		mDesc.AnyHitShaderImport = inAnyHitShaderImport;
		mDesc.ClosestHitShaderImport = inClosestHitShaderImport;
		mDesc.HitGroupExport = inHitGroupExport;
	}
};

// Local root signature - Shader input
struct LocalRootSignature : public StateSubobjectHolder<ID3D12RootSignature*, D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE>
{
	LocalRootSignature(const D3D12_ROOT_SIGNATURE_DESC& inDesc)
	{
		mRootSignature = CreateRootSignature(inDesc);
		mDesc = mRootSignature.Get();
	}
private:
	ComPtr<ID3D12RootSignature> mRootSignature; // Necessary?
};

// Associate subobject to exports - Shader entry point -> Shader input
struct SubobjectToExportsAssociation : public StateSubobjectHolder<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION, D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION>
{
	SubobjectToExportsAssociation(const wchar_t* inExportNames[], uint32_t inExportCount, const D3D12_STATE_SUBOBJECT* inSubobjectToAssociate)
	{
		mDesc.NumExports = inExportCount;
		mDesc.pExports = inExportNames;
		mDesc.pSubobjectToAssociate = inSubobjectToAssociate;
	}
};

// Shader config
struct ShaderConfig : public StateSubobjectHolder<D3D12_RAYTRACING_SHADER_CONFIG, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG>
{
	ShaderConfig(uint32_t inMaxAttributeSizeInBytes, uint32_t inMaxPayloadSizeInBytes)
	{
		mDesc.MaxAttributeSizeInBytes = inMaxAttributeSizeInBytes;
		mDesc.MaxPayloadSizeInBytes = inMaxPayloadSizeInBytes;
	}
};

// Pipeline config
struct PipelineConfig : public StateSubobjectHolder<D3D12_RAYTRACING_PIPELINE_CONFIG, D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG>
{
	PipelineConfig(uint32_t inMaxTraceRecursionDepth)
	{
		mDesc.MaxTraceRecursionDepth = inMaxTraceRecursionDepth;
	}
};

// Global root signature
struct GlobalRootSignature : public StateSubobjectHolder<ID3D12RootSignature*, D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE>
{
	GlobalRootSignature(const D3D12_ROOT_SIGNATURE_DESC& inDesc)
	{
		mRootSignature = CreateRootSignature(inDesc);
		mDesc = mRootSignature.Get();
	}
	ComPtr<ID3D12RootSignature> mRootSignature; // Necessary?
};

static const wchar_t* kRayGenShader = L"rayGen";
static const wchar_t* kMissShader = L"miss";
static const wchar_t* kClosestHitShader = L"chs";
static const wchar_t* kHitGroup = L"HitGroup";

void gValidate(HRESULT in)
{
	if (FAILED(in))
	{
		char message[512];
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, in, 0, message, ARRAYSIZE(message), nullptr);
		OutputDebugStringA(message);
		assert(false);
	}
}

void CreatePipelineState()
{
	// See D3D12_STATE_SUBOBJECT_TYPE
	// Notice all pointers should be valid until CreateStateObject

	// Need 10 subobjects:
	//  1 for the DXIL library
	//  1 for hit-group
	//  2 for RayGen root-signature (root-signature and the subobject association)
	//  2 for the root-signature shared between miss and hit shaders (signature and association)
	//  2 for shader config (shared between all programs. 1 for the config, 1 for association)
	//  1 for pipeline config
	//  1 for the global root signature
	std::array<D3D12_STATE_SUBOBJECT, 10> subobjects;
	uint32_t index = 0;

	// 1 for the DXIL library
	const wchar_t* entry_points[] = { kRayGenShader, kMissShader, kClosestHitShader };
	DXILLibrary dxilLibrary(CompileShader(L"Shader", gShaderSource, _countof(gShaderSource)), entry_points, _countof(entry_points));
	subobjects[index++] = dxilLibrary.mStateSubobject;

	// 1 for hit-group
 	HitGroup hit_group(nullptr, kClosestHitShader, kHitGroup);
 	subobjects[index++] = hit_group.mStateSubobject;
 
 	// 2 for RayGen root-signature
	RootSignatureDescriptor ray_gen_local_root_signature_desc;
	GenerateRayGenLocalRootDesc(ray_gen_local_root_signature_desc);
 	LocalRootSignature ray_gen_local_root_signature(ray_gen_local_root_signature_desc.mDesc);
 	subobjects[index++] = ray_gen_local_root_signature.mStateSubobject;

	SubobjectToExportsAssociation ray_gen_association(&kRayGenShader, 1, &(subobjects[index - 1]));
 	subobjects[index++] = ray_gen_association.mStateSubobject;
 
 	// 2 for the root-signature shared between miss and hit shaders
	RootSignatureDescriptor miss_closest_hit_local_root_signature_desc;
	miss_closest_hit_local_root_signature_desc.mDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
 	LocalRootSignature miss_closest_hit_local_root_signature(miss_closest_hit_local_root_signature_desc.mDesc);
	subobjects[index++] = miss_closest_hit_local_root_signature.mStateSubobject;
 
 	const wchar_t* miss_closest_hit_export_names[] = { kMissShader, kClosestHitShader};
 	SubobjectToExportsAssociation miss_closest_hit_association(miss_closest_hit_export_names, _countof(miss_closest_hit_export_names), &(subobjects[index - 1]));
	subobjects[index++] = miss_closest_hit_association.mStateSubobject;

 	// 2 for shader config
	ShaderConfig shader_config(sizeof(float) * 2, sizeof(float) * 1); // ???
	subobjects[index++] = shader_config.mStateSubobject;
 
 	const wchar_t* shader_exports[] = { kMissShader, kClosestHitShader, kRayGenShader }; // does order matter?
	SubobjectToExportsAssociation shader_config_association(shader_exports, _countof(shader_exports), &(subobjects[index - 1]));
	subobjects[index++] = shader_config_association.mStateSubobject;
 
 	// 1 for pipeline config
 	PipelineConfig pipeline_config(0);
	subobjects[index++] = pipeline_config.mStateSubobject;

	// 1 for the global root signature
	GlobalRootSignature global_root_signature({});
	subobjects[index++] = global_root_signature.mStateSubobject;

	// Create the state
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = (UINT)subobjects.size();
	desc.pSubobjects = subobjects.data();
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

	// Most validation occurs here
	// Be sure use correct dll for dxc compiler
	// e.g. Error "Hash check failed for DXILibrary.pShaderBytecode" appears when dxil.dll is missing.
	gValidate(g_D3DDevice->CreateStateObject(&desc, IID_PPV_ARGS(&g_StateObject)));
}