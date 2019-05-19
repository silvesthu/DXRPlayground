#include "Thirdparty/imgui/imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "CommonInclude.h"

#include "AccelerationStructure.h"
#include "PipelineState.h"
#include "ShaderResource.h"
#include "ShaderTable.h"
#include "RayTrace.h"

#define DX12_ENABLE_DEBUG_LAYER     (0)

struct FrameContext
{
	ID3D12CommandAllocator* CommandAllocator;
	UINT64                  FenceValue;
};

// Data
static int const                    NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext                	gFrameContext[NUM_FRAMES_IN_FLIGHT] = {};
static UINT                    		gFrameIndex = 0;

static int const                    NUM_BACK_BUFFERS = 3;
ID3D12Device5*						gD3DDevice = nullptr;
ID3D12DescriptorHeap*				gD3DRtvDescHeap = nullptr;
ID3D12DescriptorHeap*				gD3DSrvDescHeap = nullptr;
ID3D12CommandQueue*					gD3DCommandQueue = nullptr;
ID3D12GraphicsCommandList4*			gD3DCommandList = nullptr;
ID3D12Fence*						gFence = nullptr;
HANDLE                       		gFenceEvent = nullptr;
UINT64                       		gFenceLastSignaledValue = 0;
IDXGISwapChain3*					gSwapChain = nullptr;
HANDLE                       		gSwapChainWaitableObject = nullptr;
static ID3D12Resource*				sBackBufferRenderTargetResource[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  sBackBufferRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

// Customization - Data
static const char*					kApplicationTitle = "DXR Playground";
static const wchar_t*				kApplicationTitleW = L"DXR Playground";

uint64_t							gFenceValue = 0; // ???

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
ID3D12DescriptorHeap*				gDxrSrvUavHeap = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
FrameContext* WaitForNextFrameResources();
void ResizeSwapChain(HWND hWnd, int width, int height);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

	// Customization - Initialize
	{
		CreateVertexBuffer();
		CreateBottomLevelAccelerationStructure();
		CreateTopLevelAccelerationStructure();

		// The tutorial doesn't have any resource lifetime management, so we flush and sync here. 
		// This is not required by the DXR spec - you can submit the list whenever you like as long as you take care of the resources lifetime.
		ExecuteAccelerationStructureCreation();

		CreatePipelineState();
		CreateShaderResource();
		CreateShaderTable();
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
	ImGui_ImplDX12_Init(gD3DDevice, NUM_FRAMES_IN_FLIGHT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		gD3DSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		gD3DSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'misc/fonts/README.txt' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != nullptr);

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
				snprintf(buff, sizeof(buff), "%s - Average %.3f ms/frame (%.1f FPS)", kApplicationTitle, 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
				SetWindowTextA(hwnd, buff);
			}
		}

		// Rendering
		{
			// Wait
			FrameContext* frameCtxt = WaitForNextFrameResources();

			// Reset
			frameCtxt->CommandAllocator->Reset();

			// Frame data
			uint32_t frame_index = gSwapChain->GetCurrentBackBufferIndex();
			ID3D12Resource* frame_render_target_resource = sBackBufferRenderTargetResource[frame_index];
			D3D12_CPU_DESCRIPTOR_HANDLE& frame_render_target_descriptor = sBackBufferRenderTargetDescriptor[frame_index];

			// Frame begin
			{
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

			// Draw
			{
				gD3DCommandList->ClearRenderTargetView(frame_render_target_descriptor, (float*)& clear_color, 0, nullptr);
				RayTrace(frame_render_target_resource);
			}

			// Draw ImGui
			{
				gD3DCommandList->OMSetRenderTargets(1, &frame_render_target_descriptor, FALSE, nullptr);
				gD3DCommandList->SetDescriptorHeaps(1, &gD3DSrvDescHeap);

				// Record
				ImGui::Render();
				// Draw (Create command)
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
		{
			CleanupShaderResource();
			CleanupShaderTable();
			CleanupPipelineState();

			CleanupTopLevelAccelerationStructure();
			CleanupBottomLevelAccelerationStructure();
			CleanupVertexBuffer();
		}

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
			sBackBufferRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (gD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gD3DSrvDescHeap)) != S_OK)
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
		swapChain1->Release();
		dxgiFactory->Release();
		gSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		gSwapChainWaitableObject = gSwapChain->GetFrameLatencyWaitableObject();
	}

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();

	if (gSwapChain) { gSwapChain->Release(); gSwapChain = nullptr; }
	if (gSwapChainWaitableObject != nullptr) { CloseHandle(gSwapChainWaitableObject); }
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (gFrameContext[i].CommandAllocator) { gFrameContext[i].CommandAllocator->Release(); gFrameContext[i].CommandAllocator = nullptr; }
	if (gD3DCommandQueue) { gD3DCommandQueue->Release(); gD3DCommandQueue = nullptr; }
	if (gD3DCommandList) { gD3DCommandList->Release(); gD3DCommandList = nullptr; }
	if (gD3DRtvDescHeap) { gD3DRtvDescHeap->Release(); gD3DRtvDescHeap = nullptr; }
	if (gD3DSrvDescHeap) { gD3DSrvDescHeap->Release(); gD3DSrvDescHeap = nullptr; }
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
		gD3DDevice->CreateRenderTargetView(pBackBuffer, nullptr, sBackBufferRenderTargetDescriptor[i]);
		sBackBufferRenderTargetResource[i] = pBackBuffer;
	}
}

void CleanupRenderTarget()
{
	WaitForLastSubmittedFrame();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (sBackBufferRenderTargetResource[i]) { sBackBufferRenderTargetResource[i]->Release(); sBackBufferRenderTargetResource[i] = nullptr; }
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
