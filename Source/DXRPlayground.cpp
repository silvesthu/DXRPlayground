#include "Thirdparty/imgui/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"

#include "Common.h"

#include "Scene.h"
#include "RayTrace.h"

#define DX12_ENABLE_DEBUG_LAYER			(1)

static const wchar_t*					kApplicationTitleW = L"DXR Playground";

struct CameraSettings
{
	glm::vec2		mMoveRotateSpeed;
	float			mHorizontalFovDegree;

	CameraSettings() { Reset(); }

	void Reset()
	{
		mMoveRotateSpeed = glm::vec2(0.1f, 0.01f);
		mHorizontalFovDegree = 90.0f;
	}
};
CameraSettings		gCameraSettings = {};

struct DisplaySettings
{
	glm::ivec2		mRenderResolution	= glm::ivec2(0, 0);
	bool			mVsync				= false;
};
DisplaySettings		gDisplaySettings	= {};

// Forward declarations of helper functions
static bool sCreateDeviceD3D(HWND hWnd);
static void sCleanupDeviceD3D();
static void sCreateRenderTarget();
static void sCleanupRenderTarget();
static void sWaitForLastSubmittedFrame();
static FrameContext* sWaitForNextFrameResources();
static void sUpdate();
static void sRender();
static LRESULT WINAPI sWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void CleanupApplication()
{
	gCleanupScene();
}

void CreateApplication()
{
	gCreateScene();
}

void sUpdate()
{
	// Rotate Camera
	{
		static ImVec2 mouse_prev_position(0, 0);
		static bool mouse_prev_right_button_pressed = false;

		ImVec2 mouse_current_position = ImGui::GetMousePos();
		bool mouse_right_button_pressed = ImGui::IsMouseDown(1);

		ImVec2 mouse_delta(0, 0);
		if (mouse_prev_right_button_pressed && mouse_right_button_pressed)
			mouse_delta = ImVec2(mouse_current_position.x - mouse_prev_position.x, mouse_current_position.y - mouse_prev_position.y);

		mouse_prev_position = mouse_current_position;
		mouse_prev_right_button_pressed = mouse_right_button_pressed;

		glm::vec4 front = gPerFrameConstantBuffer.mCameraDirection;
		glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(0, 1, 0), glm::vec3(front))), 0);
		glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(right), glm::vec3(front))), 0);

		front = glm::rotate(-mouse_delta.x * gCameraSettings.mMoveRotateSpeed.y, glm::vec3(up)) * front;
		front = glm::rotate(mouse_delta.y * gCameraSettings.mMoveRotateSpeed.y, glm::vec3(right)) * front;

		gPerFrameConstantBuffer.mCameraDirection = glm::normalize(front);
		if (glm::isnan(gPerFrameConstantBuffer.mCameraDirection.x))
			gPerFrameConstantBuffer.mCameraDirection = glm::vec4(0, 0, 1, 0);
	}

	// Move Camera
	{
		float frame_speed_scale = ImGui::GetIO().DeltaTime / (1.0f / 60.0f);
		float move_speed = gCameraSettings.mMoveRotateSpeed.x * frame_speed_scale;
		if (ImGui::GetIO().KeyShift)
			move_speed *= 2.0f;
		if (ImGui::GetIO().KeyCtrl)
			move_speed *= 0.1f;

		if (ImGui::IsKeyDown('W'))
			gPerFrameConstantBuffer.mCameraPosition += gPerFrameConstantBuffer.mCameraDirection * move_speed;
		if (ImGui::IsKeyDown('S'))
			gPerFrameConstantBuffer.mCameraPosition -= gPerFrameConstantBuffer.mCameraDirection * move_speed;

		glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(0, 1, 0), glm::vec3(gPerFrameConstantBuffer.mCameraDirection))), 0);

		if (ImGui::IsKeyDown('A'))
			gPerFrameConstantBuffer.mCameraPosition -= right * move_speed;
		if (ImGui::IsKeyDown('D'))
			gPerFrameConstantBuffer.mCameraPosition += right * move_speed;
	}

	// Frustum
	{
		float horizontal_tan = glm::tan(gCameraSettings.mHorizontalFovDegree * 0.5f * glm::pi<float>() / 180.0f);

		glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(0, 1, 0), glm::vec3(gPerFrameConstantBuffer.mCameraDirection))), 0);
		gPerFrameConstantBuffer.mCameraRightExtend = right * horizontal_tan;

		glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(right), glm::vec3(gPerFrameConstantBuffer.mCameraDirection))), 0);
		gPerFrameConstantBuffer.mCameraUpExtend = up * horizontal_tan * (gDisplaySettings.mRenderResolution.y * 1.0f / gDisplaySettings.mRenderResolution.x);
	}

	// ImGUI
	{
		ImGui::Begin("DXR Playground");
		{
			ImGui::Text("Average %.3f ms/frame (%.1f FPS) @ %dx%d",
				1000.0f / ImGui::GetIO().Framerate,
				ImGui::GetIO().Framerate,
				gDisplaySettings.mRenderResolution.x,
				gDisplaySettings.mRenderResolution.y);

			if (ImGui::TreeNodeEx("Camera"))
			{
				if (ImGui::Button("Reset"))
				{
					gPerFrameConstantBuffer.mCameraPosition = glm::vec4(0, 0, -5, 0);
					gPerFrameConstantBuffer.mCameraDirection = glm::vec4(0, 0, 1, 0);

					gCameraSettings.Reset();
				}
				ImGui::InputFloat3("Position", (float*)&gPerFrameConstantBuffer.mCameraPosition);
				ImGui::InputFloat3("Direction", (float*)&gPerFrameConstantBuffer.mCameraDirection);
				ImGui::SliderFloat("Horz Fov", (float*)&gCameraSettings.mHorizontalFovDegree, 30.0f, 160.0f);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Display"))
			{
				ImGui::Checkbox("Vsync", &gDisplaySettings.mVsync);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Background"))
			{
				ImGui::ColorEdit3("Color", (float*)&gPerFrameConstantBuffer.mBackgroundColor);

				ImGui::TreePop();
			}
		}
		ImGui::End();
	}
}

// Main code
int WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, PSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
	// Create application window
	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, sWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, kApplicationTitleW, nullptr };
	::RegisterClassEx(&wc);
	HWND hwnd = ::CreateWindow(wc.lpszClassName, kApplicationTitleW, WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

	// Initialize Direct3D
	if (!sCreateDeviceD3D(hwnd))
	{
		sCleanupDeviceD3D();
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}
	sCreateRenderTarget();

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
	ImGui_ImplDX12_Init(gDevice, NUM_FRAMES_IN_FLIGHT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		gImGuiSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		gImGuiSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	// Main loop
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
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

		sUpdate();
		sRender();
	}

	// Shutdown
	{
		sWaitForLastSubmittedFrame();
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		// Customization - Cleanup
		CleanupApplication();

		sCleanupRenderTarget();
		sCleanupDeviceD3D();
		::DestroyWindow(hwnd);
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
	}

	return 0;
}

void sRender()
{
	// Frame context
	FrameContext* frameCtxt = sWaitForNextFrameResources();
	uint32_t frame_index = gSwapChain->GetCurrentBackBufferIndex();
	ID3D12Resource* frame_render_target_resource = gBackBufferRenderTargetResource[frame_index];
	D3D12_CPU_DESCRIPTOR_HANDLE& frame_render_target_descriptor = gBackBufferRenderTargetDescriptor[frame_index];

	// Frame begin
	{
		frameCtxt->mCommandAllocator->Reset();
		gCommandList->Reset(frameCtxt->mCommandAllocator, nullptr);
		
		gBarrierTransition(gCommandList, frame_render_target_resource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	// Update and Upload
	{
		gUpdateScene();
	}

	// Upload
	{
		memcpy(frameCtxt->mConstantUploadBufferPointer, &gPerFrameConstantBuffer, sizeof(gPerFrameConstantBuffer));

		gBarrierTransition(gCommandList, gConstantGPUBuffer, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		gCommandList->CopyResource(gConstantGPUBuffer, frameCtxt->mConstantUploadBuffer);
		gBarrierTransition(gCommandList, gConstantGPUBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	}

	// Raytrace
	{
		gRaytrace(frame_render_target_resource);
	}

	// Draw ImGui
	{
		gCommandList->OMSetRenderTargets(1, &frame_render_target_descriptor, FALSE, nullptr);
		gCommandList->SetDescriptorHeaps(1, &gImGuiSrvDescHeap);

		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);
	}

	// Frame end
	{
		gBarrierTransition(gCommandList, frame_render_target_resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		gCommandList->Close();
		gCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&gCommandList);

		gTime += ImGui::GetIO().DeltaTime;
	}

	// Swap
	{
		if (gDisplaySettings.mVsync)
			gSwapChain->Present(1, 0);
 		else
			gSwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

		UINT64 fenceValue = gFenceLastSignaledValue + 1;
		gCommandQueue->Signal(gIncrementalFence, fenceValue);
		gFenceLastSignaledValue = fenceValue;
		frameCtxt->mFenceValue = fenceValue;
	}
}

// Helper functions
static bool sCreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	{
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = NUM_BACK_BUFFERS;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
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
		ComPtr<ID3D12Debug> dx12Debug = nullptr;
		ComPtr<ID3D12Debug1> dx12Debug1 = nullptr;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12Debug))))
		{
			dx12Debug->EnableDebugLayer();

			if (SUCCEEDED(dx12Debug->QueryInterface(IID_PPV_ARGS(&dx12Debug1))))
				dx12Debug1->SetEnableGPUBasedValidation(true);
		}
	}

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_0;
	if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&gDevice)) != S_OK)
		return false;

	// Check DXR
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5;
		memset(&features5, 0, sizeof(features5));
		HRESULT hr = gDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
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
		if (gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gRtvDescHeap)) != S_OK)
			return false;

		SIZE_T rtvDescriptorSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = gRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
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
		if (gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gImGuiSrvDescHeap)) != S_OK)
			return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		if (gDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&gCommandQueue)) != S_OK)
			return false;
	}

	// PerFrame
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
	{
		std::wstring name;

		gValidate(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gFrameContext[i].mCommandAllocator)) != S_OK);
		name = L"CommandAllocator_" + i;
		gFrameContext[i].mCommandAllocator->SetName(name.c_str());

		// Create CBV resource
		{
			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Alignment = 0;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
			resource_desc.Format = DXGI_FORMAT_UNKNOWN;
			resource_desc.Width = gAlignUp((UINT)sizeof(PerFrame), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
			resource_desc.Height = 1;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resource_desc.MipLevels = 1;
			resource_desc.SampleDesc.Count = 1;
			resource_desc.SampleDesc.Quality = 0;

			D3D12_HEAP_PROPERTIES heap_props;
			heap_props.Type = D3D12_HEAP_TYPE_UPLOAD; // https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_heap_type#constants
			heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heap_props.CreationNodeMask = 0;
			heap_props.VisibleNodeMask = 0;

			gValidate(gDevice->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gFrameContext[i].mConstantUploadBuffer)));
			name = L"Constant_Upload_" + i;
			gFrameContext[i].mConstantUploadBuffer->SetName(name.c_str());

			// Persistent map - https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map#advanced-usage-models
			gFrameContext[i].mConstantUploadBuffer->Map(0, nullptr, (void**)&gFrameContext[i].mConstantUploadBufferPointer);
		}
	}

	// GPU
	{
		std::wstring name;

		// Create CBV resource
		{
			D3D12_RESOURCE_DESC resource_desc = {};
			resource_desc.Alignment = 0;
			resource_desc.DepthOrArraySize = 1;
			resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
			resource_desc.Format = DXGI_FORMAT_UNKNOWN;
			resource_desc.Width = gAlignUp((UINT)sizeof(PerFrame), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
			resource_desc.Height = 1;
			resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resource_desc.MipLevels = 1;
			resource_desc.SampleDesc.Count = 1;
			resource_desc.SampleDesc.Quality = 0;

			D3D12_HEAP_PROPERTIES heap_props;
			heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
			heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heap_props.CreationNodeMask = 0;
			heap_props.VisibleNodeMask = 0;

			gValidate(gDevice->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&gConstantGPUBuffer)));
			name = L"Constant_GPU";
			gConstantGPUBuffer->SetName(name.c_str());
		}
	}

	gValidate(gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gFrameContext[0].mCommandAllocator, nullptr, IID_PPV_ARGS(&gCommandList)) != S_OK);
	gCommandList->SetName(L"CommandList");

	if (gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gIncrementalFence)) != S_OK)
		return false;

	gIncrementalFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (gIncrementalFenceEvent == nullptr)
		return false;

	{
		ComPtr<IDXGIFactory4> dxgiFactory = nullptr;
		ComPtr<IDXGISwapChain1> swapChain1 = nullptr;

		UINT flags = 0;
		if (DX12_ENABLE_DEBUG_LAYER)
			flags = DXGI_CREATE_FACTORY_DEBUG;

		if (CreateDXGIFactory2(flags, IID_PPV_ARGS(&dxgiFactory)) != S_OK ||
			dxgiFactory->CreateSwapChainForHwnd(gCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK ||
			swapChain1->QueryInterface(IID_PPV_ARGS(&gSwapChain)) != S_OK)
			return false;

		// Fullscreen -> Windowed cause crash on resource reference in WM_SIZE handling, disable fullscreen for now
		dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

		gSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		gSwapChainWaitableObject = gSwapChain->GetFrameLatencyWaitableObject();
	}

	return true;
}

static void sCleanupDeviceD3D()
{
	gSafeRelease(gSwapChain);
	gSafeCloseHandle(gSwapChainWaitableObject);

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
	{
		gSafeRelease(gFrameContext[i].mCommandAllocator);
		gSafeRelease(gFrameContext[i].mConstantUploadBuffer);
	}

	gSafeRelease(gConstantGPUBuffer);
	gSafeRelease(gCommandQueue);
	gSafeRelease(gCommandList);
	gSafeRelease(gRtvDescHeap);
	gSafeRelease(gImGuiSrvDescHeap);
	gSafeRelease(gIncrementalFence);
	gSafeCloseHandle(gIncrementalFenceEvent);
	gSafeRelease(gDevice);

	if (DX12_ENABLE_DEBUG_LAYER)
	{
		ComPtr<IDXGIDebug1> dxgi_debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
			dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
	}
}

static void sCreateRenderTarget()
{
	ID3D12Resource* pBackBuffer;
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		gDevice->CreateRenderTargetView(pBackBuffer, nullptr, gBackBufferRenderTargetDescriptor[i]);
		gBackBufferRenderTargetResource[i] = pBackBuffer;
	}
}

static void sCleanupRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		gSafeRelease(gBackBufferRenderTargetResource[i]);
}

static void sWaitForLastSubmittedFrame()
{
	FrameContext* frameCtxt = &gFrameContext[gFrameIndex % NUM_FRAMES_IN_FLIGHT];

	UINT64 fenceValue = frameCtxt->mFenceValue;
	if (fenceValue == 0)
		return; // No fence was signaled

	frameCtxt->mFenceValue = 0;
	if (gIncrementalFence->GetCompletedValue() >= fenceValue)
		return;

	gIncrementalFence->SetEventOnCompletion(fenceValue, gIncrementalFenceEvent);
	WaitForSingleObject(gIncrementalFenceEvent, INFINITE);
}

static FrameContext* sWaitForNextFrameResources()
{
	UINT nextFrameIndex = gFrameIndex + 1;
	gFrameIndex = nextFrameIndex;

	HANDLE waitableObjects[] = { gSwapChainWaitableObject, nullptr };
	DWORD numWaitableObjects = 1;

	FrameContext* frameCtxt = &gFrameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
	UINT64 fenceValue = frameCtxt->mFenceValue;
	if (fenceValue != 0) // means no fence was signaled
	{
		frameCtxt->mFenceValue = 0;
		gIncrementalFence->SetEventOnCompletion(fenceValue, gIncrementalFenceEvent);
		waitableObjects[1] = gIncrementalFenceEvent;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

	return frameCtxt;
}

// Win32 message handler
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI sWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (gDevice != nullptr && wParam != SIZE_MINIMIZED)
		{
			sWaitForLastSubmittedFrame();

			gRebuildBinding([&]()
			{
				sCleanupRenderTarget();
				// ImGui sample re-create swap chain and stop DXGI_MWA_NO_ALT_ENTER from working
				gDisplaySettings.mRenderResolution.x = gMax((UINT)LOWORD(lParam), 8u);
				gDisplaySettings.mRenderResolution.y = gMax((UINT)HIWORD(lParam), 8u);
				gSwapChain->ResizeBuffers(
					NUM_BACK_BUFFERS,
					gDisplaySettings.mRenderResolution.x,
					gDisplaySettings.mRenderResolution.y,
					DXGI_FORMAT_R8G8B8A8_UNORM,
					DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
				sCreateRenderTarget();
			});
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
