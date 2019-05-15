#include "Thirdparty/imgui/imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <tchar.h>
#include <string>
#include <iostream>
#include <array>

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
static ID3D12DescriptorHeap*		g_D3dRtvDescHeap = NULL;
static ID3D12DescriptorHeap*		g_D3dSrvDescHeap = NULL;
static ID3D12CommandQueue*			g_D3dCommandQueue = NULL;
static ID3D12GraphicsCommandList4*	g_D3dCommandList = NULL;
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
static uint64_t						g_FenceValue;
static ID3D12Resource*				g_VertexBuffer;
static ID3D12Resource*				g_BottomLevelAccelerationStructureScratch;
static ID3D12Resource*				g_BottomLevelAccelerationStructureDest;
static ID3D12Resource*				g_TopLevelAccelerationStructureScratch;
static ID3D12Resource*				g_TopLevelAccelerationStructureDest;
static ID3D12Resource*				g_TopLevelAccelerationStructureInstanceDesc;

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
	CreateVertexBuffer();
	CreateBottomLevelAccelerationStructure();
	CreateTopLevelAccelerationStructure();

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
		g_D3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		g_D3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

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

				g_D3dCommandList->Reset(frameCtxt->CommandAllocator, NULL);
				g_D3dCommandList->ResourceBarrier(1, &barrier);
				g_D3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], (float*)& clear_color, 0, NULL);
				g_D3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, NULL);
				g_D3dCommandList->SetDescriptorHeaps(1, &g_D3dSrvDescHeap);
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
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_D3dCommandList);
			}

			// Frame end
			{
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
				g_D3dCommandList->ResourceBarrier(1, &barrier);
				g_D3dCommandList->Close();
				g_D3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)& g_D3dCommandList);
			}			

			// Swap
			{
				g_SwapChain->Present(1, 0); // Present with vsync
				//g_SwapChain->Present(0, 0); // Present without vsync

				UINT64 fenceValue = g_FenceLastSignaledValue + 1;
				g_D3dCommandQueue->Signal(g_Fence, fenceValue);
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
	DXGI_SWAP_CHAIN_DESC1 sd;
	{
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = NUM_BACK_BUFFERS;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
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
		if (g_D3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_D3dRtvDescHeap)) != S_OK)
			return false;

		SIZE_T rtvDescriptorSize = g_D3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_D3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
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
		if (g_D3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_D3dSrvDescHeap)) != S_OK)
			return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		if (g_D3DDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_D3dCommandQueue)) != S_OK)
			return false;
	}

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (g_D3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_FrameContext[i].CommandAllocator)) != S_OK)
			return false;

	if (g_D3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_FrameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&g_D3dCommandList)) != S_OK ||
		g_D3dCommandList->Close() != S_OK)
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
			dxgiFactory->CreateSwapChainForHwnd(g_D3dCommandQueue, hWnd, &sd, NULL, NULL, &swapChain1) != S_OK ||
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
	if (g_D3dCommandQueue) { g_D3dCommandQueue->Release(); g_D3dCommandQueue = NULL; }
	if (g_D3dCommandList) { g_D3dCommandList->Release(); g_D3dCommandList = NULL; }
	if (g_D3dRtvDescHeap) { g_D3dRtvDescHeap->Release(); g_D3dRtvDescHeap = NULL; }
	if (g_D3dSrvDescHeap) { g_D3dSrvDescHeap->Release(); g_D3dSrvDescHeap = NULL; }
	if (g_Fence) { g_Fence->Release(); g_Fence = NULL; }
	if (g_FenceEvent) { CloseHandle(g_FenceEvent); g_FenceEvent = NULL; }
	if (g_D3DDevice) { g_D3DDevice->Release(); g_D3DDevice = NULL; }
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
	dxgiFactory->CreateSwapChainForHwnd(g_D3dCommandQueue, hWnd, &sd, NULL, NULL, &swapChain1);
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

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
	g_D3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	// Create the buffers. They need to support UAV, and since we are going to immediately use them, we create them with an unordered-access state
	{
		D3D12_HEAP_PROPERTIES props;
		memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
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

		g_D3dCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	}

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = g_BottomLevelAccelerationStructureDest;
		g_D3dCommandList->ResourceBarrier(1, &uavBarrier);
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

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	g_D3DDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	// Create the buffers
	{
		D3D12_HEAP_PROPERTIES props;
		memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
		props.Type = D3D12_HEAP_TYPE_DEFAULT;
		props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
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

		g_D3dCommandList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
	}

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = g_TopLevelAccelerationStructureDest;
		g_D3dCommandList->ResourceBarrier(1, &uavBarrier);
	}
}

void CleanupTopLevelAccelerationStructure()
{
	gSafeRelease(g_TopLevelAccelerationStructureScratch);
	gSafeRelease(g_TopLevelAccelerationStructureDest);
	gSafeRelease(g_TopLevelAccelerationStructureInstanceDesc);
}