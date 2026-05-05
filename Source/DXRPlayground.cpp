#include "Common.h"

#include "Renderer.h"
#include "Color.h"
#include "Scene.h"
#include "GUI.h"

#include "Atmosphere.h"
#include "Cloud.h"

#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"

#pragma warning(push)
#pragma warning(disable: 4068)
#include "Thirdparty/filewatch/FileWatch.hpp"
#pragma warning(pop)

#include "wincodec.h" // GUID_ContainerFormatPng

extern "C" { __declspec(dllexport) extern const UINT			D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char8_t*		D3D12SDKPath = u8".\\D3D12\\"; }

#define DX12_ENABLE_DEBUG_LAYER			(0)
#define DX12_ENABLE_INFO_QUEUE_CALLBACK (0)
#define DX12_ENABLE_GBV					(0)

static const wchar_t*											kApplicationTitleW = L"DXR Playground";
static const std::wstring										kINIPathStringW = std::filesystem::absolute(L"DXRPlayground.ini").wstring();
static const wchar_t*											kINIPathW = kINIPathStringW.c_str();

// Forward declarations of helper functions
static bool sCreateDeviceD3D(HWND hWnd);
static void sCleanupDeviceD3D();
static void sWaitForGPU();
static void sUpdate();
static void sLoadScene(bool inLoadCamera);
static void sRender();
static int sStartup(WNDCLASSEX& wc, HWND& hwnd);
static LRESULT WINAPI sWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void sUpdate()
{	
	// Resize
	if (gRenderer.mScreenSizeRequested != uint2{ 0, 0 })
	{
		RECT rect = { 0, 0, (LONG)gRenderer.mScreenSizeRequested.x, (LONG)gRenderer.mScreenSizeRequested.y };
		AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
		SetWindowPos(::GetActiveWindow(), NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);

		gRenderer.mScreenSizeRequested = { 0, 0 };
	}

	// Rotate Camera
	if (!gHeadless)
	{
		using namespace ImGui;

		static ImVec2 mouse_prev_position(0, 0);
		static bool mouse_prev_right_button_pressed = false;

		ImVec2 mouse_current_position = GetMousePos();
		bool mouse_right_button_pressed = IsMouseDown(ImGuiMouseButton_Right);

		ImVec2 mouse_delta(0, 0);
		if (mouse_prev_right_button_pressed && mouse_right_button_pressed)
			mouse_delta = ImVec2(mouse_current_position.x - mouse_prev_position.x, mouse_current_position.y - mouse_prev_position.y);

		mouse_prev_position = mouse_current_position;
		mouse_prev_right_button_pressed = mouse_right_button_pressed;

		if (mouse_delta.x != 0 || mouse_delta.y != 0) // otherwise result of glm::normalize might oscillate
		{
			float4 front						= gConstants.CameraFront();
			float4 left							= float4(glm::normalize(glm::cross(float3(0, 1, 0), float3(gConstants.CameraFront()))), 0);
			float4 up							= float4(glm::normalize(glm::cross(float3(gConstants.CameraFront()), float3(left))), 0);

			front								= glm::rotate(-mouse_delta.x * gCameraSettings.mRotateSpeed, float3(up)) * front;
			front								= glm::rotate(mouse_delta.y * gCameraSettings.mRotateSpeed, float3(left)) * front;

			gConstants.CameraFront()			= glm::normalize(front);
			gConstants.CameraLeft()				= glm::normalize(float4(glm::cross(float3(0, 1, 0), float3(gConstants.CameraFront())), 0));
			gConstants.CameraUp()				= glm::normalize(float4(glm::cross(float3(gConstants.CameraFront()), float3(gConstants.CameraLeft())), 0));
		}
	}

	// Move Camera
	if (!gHeadless)
	{
		using namespace ImGui;

		float frame_speed_scale = GetIO().DeltaTime / (1.0f / 60.0f);
		float move_speed = gCameraSettings.mMoveSpeed * frame_speed_scale;
		if (GetIO().KeyShift)
			move_speed *= 20.0f;
		if (GetIO().KeyCtrl)
			move_speed *= 0.1f;

		if (IsKeyDown(ImGuiKey::ImGuiKey_W))
			gConstants.CameraPosition() += gConstants.CameraFront() * move_speed;
		if (IsKeyDown(ImGuiKey::ImGuiKey_S))
			gConstants.CameraPosition() -= gConstants.CameraFront() * move_speed;

		glm::vec4 left = glm::vec4(glm::normalize(glm::cross(glm::vec3(0, 1, 0), glm::vec3(gConstants.CameraFront()))), 0);

		if (IsKeyDown(ImGuiKey::ImGuiKey_A))
			gConstants.CameraPosition() += left * move_speed;
		if (IsKeyDown(ImGuiKey::ImGuiKey_D))
			gConstants.CameraPosition() -= left * move_speed;

		glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(left), glm::vec3(gConstants.CameraFront()))), 0);

		if (IsKeyDown(ImGuiKey::ImGuiKey_Q))
			gConstants.CameraPosition() += up * move_speed;
		if (IsKeyDown(ImGuiKey::ImGuiKey_E))
			gConstants.CameraPosition() -= up * move_speed;

		if (IsKeyPressed(ImGuiKey::ImGuiKey_F4))
			gRenderer.mReloadScene = true;

		if (IsKeyPressed(ImGuiKey::ImGuiKey_F5))
			gRenderer.mReloadShader = true;

		if (IsKeyPressed(ImGuiKey::ImGuiKey_F6))
			gLoadCamera();

		if (IsKeyPressed(ImGuiKey::ImGuiKey_F9))
			gDumpLuminance();

		if (IsKeyPressed(ImGuiKey::ImGuiKey_F10))
			gOpenDumpDirectoryInExplorer();

		if (!IsAnyItemFocused() && IsKeyPressed(ImGuiKey::ImGuiKey_UpArrow))
			gConstants.mPixelDebugCoord -= int2(0, 1);
		if (!IsAnyItemFocused() && IsKeyPressed(ImGuiKey::ImGuiKey_DownArrow))
			gConstants.mPixelDebugCoord += int2(0, 1);
		if (!IsAnyItemFocused() && IsKeyPressed(ImGuiKey::ImGuiKey_LeftArrow))
			gConstants.mPixelDebugCoord -= int2(1, 0);
		if (!IsAnyItemFocused() && IsKeyPressed(ImGuiKey::ImGuiKey_RightArrow))
			gConstants.mPixelDebugCoord += int2(1, 0);
	}

	// Common
	{
		gConstants.mScreenWidth					= gRenderer.mScreenSize.x;
		gConstants.mScreenHeight				= gRenderer.mScreenSize.y;

		gConstants.mFrameIndex					= gFrameIndex;
	}

	// Sequence
	{
		gConstants.mSequenceFrameRatio			= gConstants.mSequenceFrameIndex * 1.0f / gConstants.mSequenceFrameCount;

		bool apply_sequence_camera				= gConstants.mSequenceEnabled && (gRenderer.mSequenceFrameRecording >= 0 || gRenderer.mSequenceCameraEnabled);

		// Camera Animation
		const SceneContent::Camera& camera		= gScene.GetSceneContent().mCamera;
		if (apply_sequence_camera && camera.mHasAnimation)
		{
			if (!camera.mAnimation.mTranslation.empty())
			{
				auto [from, to, fraction]		= gMakeLerpTuple(camera.mAnimation.mTranslation, gConstants.mSequenceFrameRatio);
				gConstants.CameraPosition()		= glm::vec4(glm::lerp(from, to, fraction), 1);
			}
			if (!camera.mAnimation.mRotation.empty())
			{
				constexpr glm::vec3 cLeft		= glm::vec3(1.0f, 0.0f, 0.0f);
				constexpr glm::vec3 cFront		= glm::vec3(0.0f, 0.0f, -1.0f);
				constexpr glm::vec3 cUp			= glm::vec3(0.0f, 1.0f, 0.0f);

				auto [from, to, fraction]		= gMakeLerpTuple(camera.mAnimation.mRotation, gConstants.mSequenceFrameRatio);
				glm::quat rotation				= glm::slerp(glm::quat(from.w, from), glm::quat(to.w, to), fraction); // glm::quat is wxyz, gltf quat is xyzw

				gConstants.CameraLeft()			= glm::vec4(rotation * cLeft, 0);
				gConstants.CameraUp()			= glm::vec4(rotation * cUp, 0);
				gConstants.CameraFront()		= glm::vec4(rotation * cFront, 0);
			}
			// mAnimation.mScale is ignored
		}
	}

	// Setup matrices
	{
		float horizontal_fov_radian				= gCameraSettings.mHorizontalFovDegree * glm::pi<float>() / 180.0f;
		float horizontal_tan					= glm::tan(horizontal_fov_radian * 0.5f);
		float vertical_tan						= horizontal_tan * (gConstants.mScreenHeight * 1.0f / gConstants.mScreenWidth);
		float vertical_fov_radian				= glm::atan(vertical_tan) * 2.0f;
		
		gConstants.mViewMatrix					= glm::lookAtRH(float3(gConstants.CameraPosition()), float3(gConstants.CameraPosition() + gConstants.CameraFront()), float3(gConstants.CameraUp()));
		gConstants.mProjectionMatrix			= glm::perspectiveFovRH_ZO(vertical_fov_radian, (float)gConstants.mScreenWidth, (float)gConstants.mScreenHeight, 0.1f, 1000.0f);
		gConstants.mViewProjectionMatrix		= gConstants.mProjectionMatrix * gConstants.mViewMatrix;

		gConstants.mInverseViewMatrix			= glm::inverse(gConstants.mViewMatrix);
		gConstants.mInverseProjectionMatrix		= glm::inverse(gConstants.mProjectionMatrix);
		gConstants.mInverseViewProjectionMatrix = glm::inverse(gConstants.mViewProjectionMatrix);
	}

	gAtmosphere.Update();
	gCloud.Update();
}

// Main code
int WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, PSTR lpCmdLine, int /*nCmdShow*/)
{
	HWND hwnd = 0;
	WNDCLASSEX wc = {};

	if (lpCmdLine != nullptr && std::string_view(lpCmdLine).starts_with("-headless"))
		gHeadless = true;

	CPUTimingScope application_timing_scope;
	application_timing_scope.mTraceName = "Application";
	application_timing_scope.mFileName = gHeadless ? "stat.txt" : "";

	int error_code = sStartup(wc, hwnd);
	if (error_code != 0) return error_code;

	// Main loop
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		if (!gHeadless && ::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			continue;
		}

		// New frame	
		gFrameIndex++;

		if (!gHeadless)
		{
			// Start the Dear ImGui frame
			ImGui_ImplDX12_FontTextureID = (ImTextureID)gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::ImGuiFont).ptr;
			ImGui_ImplDX12_NullTexture2D = (ImTextureID)gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::ImGuiNull2D).ptr;
			ImGui_ImplDX12_NullTexture3D = (ImTextureID)gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::ImGuiNull3D).ptr;
			ImGui::GetIO().Fonts->SetTexID(ImGui_ImplDX12_FontTextureID);
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
		}

		sUpdate();
		sRender();

		if (gHeadless && gHeadlessDone)
			break;
	}

	// Shutdown
	{
		sWaitForGPU();

		gAtmosphere.Finalize();
		gCloud.Finalize();

		if (!gHeadless)
		{
			ImPlot::DestroyContext();
			ImGui_ImplDX12_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}

		gScene.Unload();

		gRenderer.Finalize();

		sCleanupDeviceD3D();

		if (!gHeadless)
		{
			::DestroyWindow(hwnd);
			::UnregisterClass(wc.lpszClassName, wc.hInstance);
		}
	}

	return 0;
}

int sStartup(WNDCLASSEX& wc, HWND& hwnd)
{
	CPU_TIMING_SCOPE("Startup", &gStats.mCPUTimingMS.mStartup);

	if (gHeadless)
	{
		gConfigs.mShaderDebug = false;
		gConfigs.mUseTexture = false;
		gDisplaySettings.mVsync = false;
		gConstants.mOffsetMode = OffsetMode::Random;
		gRenderer.mRuntime.mScreenColorTexture.mFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	}
	else
	{
		// INI
		int window_x = GetPrivateProfileInt(L"Main", L"Window_X", 100, kINIPathW);
		int window_y = GetPrivateProfileInt(L"Main", L"Window_Y", 100, kINIPathW);

		// Create application window
		wc = { sizeof(WNDCLASSEX), CS_CLASSDC, sWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, kApplicationTitleW, nullptr };
		::RegisterClassEx(&wc);

		RECT rect = { 0, 0, static_cast<LONG>(Renderer().mScreenSize.x), static_cast<LONG>(Renderer().mScreenSize.y) };
		AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
		hwnd = ::CreateWindow(wc.lpszClassName, kApplicationTitleW, WS_OVERLAPPEDWINDOW, window_x, window_y, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, nullptr);
	}

	// Initialize Direct3D
	if (!sCreateDeviceD3D(hwnd))
	{
		sCleanupDeviceD3D();
		if (!gHeadless)
			::UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	if (!gHeadless)
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImPlot::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		// io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
		// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();

		ImGui_ImplDX12_CreateShaderResourceViewCallback = [](ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
			{
				for (glm::uint i = 0; i < kFrameInFlightCount; i++)
				{
					gDevice->CreateShaderResourceView(resource, &desc, gFrameContexts[i].mViewDescriptorHeap.GetCPUHandle(ViewDescriptorIndex::ImGuiFont));

					D3D12_SHADER_RESOURCE_VIEW_DESC null_desc = {};
					null_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
					null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					gDevice->CreateShaderResourceView(nullptr, &null_desc, gFrameContexts[i].mViewDescriptorHeap.GetCPUHandle(ViewDescriptorIndex::ImGuiNull2D));
					null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
					gDevice->CreateShaderResourceView(nullptr, &null_desc, gFrameContexts[i].mViewDescriptorHeap.GetCPUHandle(ViewDescriptorIndex::ImGuiNull3D));
				}
			};

		// Setup Platform/Renderer bindings
		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX12_Init(gDevice, kFrameInFlightCount, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, {}, {});
		{
			// DPI
			UINT dpi = GetDpiForWindow(hwnd);
			float scale = dpi * 1.0f / USER_DEFAULT_SCREEN_DPI;
			ImGui::GetStyle().ScaleAllSizes(scale / ImGui::gDpiScale);
			ImGui::gDpiScale = scale;

			// [TODO] This should also update on WM_DPICHANGED, which requires rebuild of font texture
			std::filesystem::path font_path = "C:\\Windows\\Fonts\\Consola.ttf";
			ImGui::GetIO().Fonts->AddFontFromFileTTF(font_path.string().c_str(), 13 * scale, nullptr, nullptr);
			ImGui::GetIO().Fonts->Build();
		}
		ImGui_ImplDX12_CreateDeviceObjects();
	}

	// Renderer
	gRenderer.Initialize();

	// Load Scene
	sLoadScene(true);

	// Features (rely on ImGui, Scene)
	gAtmosphere.Initialize();
	gCloud.Initialize();

	// File watch
	std::string shader_directory = std::filesystem::canonical("Shader\\").string(); // canonical to follow symbol link
	static filewatch::FileWatch<std::string> file_watch(shader_directory,
		[](const std::string& inPath, const filewatch::Event inChangeType)
		{
			(void)inChangeType;
			std::regex pattern(".*\\.(hlsl|hlsli|hpp|h|inl|slang)");
			if (std::regex_match(inPath, pattern) && inChangeType == filewatch::Event::modified)
			{
				std::string msg = "Reload triggered by " + inPath + "\n";
				gTrace(msg.c_str());

				gRenderer.mReloadShader = true;
			}
		});

	gCommandList->Close();
	gCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&gCommandList));
	UINT64 signal_value = ++gFenceLastSignaledValue;
	gCommandQueue->Signal(gIncrementalFence, signal_value); // abuse fence to wait only during initialization
	gIncrementalFence->SetEventOnCompletion(signal_value, gIncrementalFenceEvent);
	WaitForSingleObject(gIncrementalFenceEvent, INFINITE);

	if (!gHeadless)
	{
		// Show the window
		::ShowWindow(hwnd, SW_SHOWDEFAULT);
		::UpdateWindow(hwnd);
	}

	if (gHeadless)
	{
		// Start Sequence
		gConstants.mSequenceEnabled = 1;
		gConstants.mCurrentFrameIndex = 0;
		gConstants.mSequenceFrameIndex = 0;

		gRenderer.mSequenceFrameRecording = 0;
	}

	return 0;
}

void sLoadScene(bool inLoadCamera)
{
	CPUTimingScope timing_scope;
	timing_scope.mTraceName = "sLoadScene";

	sWaitForGPU();

	auto& preset = ScenePreset::sCurrent();

	gScene.Unload();
	gScene.Load(preset);
	gScene.UpdateGPU(gCommandList);

	gConstants.mSunAzimuth = preset.mSunAzimuth;
	gConstants.mSunZenith = preset.mSunZenith;

	gAtmosphere.mProfile.mMode = preset.mAtmosphere;
	if (gScene.GetSceneContent().mAtmosphereMode.has_value())
		gAtmosphere.mProfile.mMode = gScene.GetSceneContent().mAtmosphereMode.value();
	gAtmosphere.mProfile.mConstantColor = preset.mConstantColor;

	gRenderer.mReloadShader = true;
	gRenderer.mAccumulationResetRequested = true;
	gRenderer.mSpatialCacheResetRequested = true;

	if (inLoadCamera)
		gLoadCamera();
}

void sRender()
{
	HANDLE wait_objects[]						= { nullptr, nullptr };
	DWORD wait_object_count						= 0;
	if (!gHeadless)
	{
		wait_objects[wait_object_count++]		= gSwapChainWaitableObject;
	}
	FrameContext& frame_context					= gGetFrameContext();
	UINT64 wait_value							= frame_context.mFenceValue;
	UINT64 completed_value						= gIncrementalFence->GetCompletedValue();
	if (wait_value != 0 && completed_value < wait_value) // 0 means no fence was signaled
	{
		gIncrementalFence->SetEventOnCompletion(frame_context.mFenceValue, gIncrementalFenceEvent);
		wait_objects[wait_object_count++]		= gIncrementalFenceEvent;
	}
	WaitForMultipleObjects(wait_object_count, wait_objects, TRUE, INFINITE);

	// Frame Context
	uint32_t back_buffer_index					= 0;
	ID3D12Resource* back_buffer					= nullptr;

	if (!gHeadless)
	{
		back_buffer_index						= gSwapChain->GetCurrentBackBufferIndex();
		back_buffer								= gRenderer.mRuntime.mBackBuffers[back_buffer_index].mResource.Get();
	}

	// Frame Begin
	ID3D12GraphicsCommandList4* command_list = gCommandList;
	{
		frame_context.mCommandAllocator->Reset();
		command_list->Reset(frame_context.mCommandAllocator.Get(), nullptr);
	}

	// Reload Scene
	{
		if (ScenePreset::sPreviousIndex != ScenePreset::sCurrentIndex)
		{
			ScenePreset::sPreviousIndex = ScenePreset::sCurrentIndex;
			sLoadScene(true);
		}
		else if (gRenderer.mReloadScene)
		{
			gRenderer.mReloadScene = false;
			sLoadScene(false);
		}
	}

	// Reload Shader
	if (gRenderer.mReloadShader)
	{
		gRenderer.mReloadShader = false;

		sWaitForGPU();

		gRenderer.FinalizeShaders();
		gRenderer.InitializeShaders();

		gRenderer.mAccumulationResetRequested = true;

		gAtmosphere.mRuntime.mBruneton17.mRecomputeRequested = true;
		gCloud.mRecomputeRequested = true;
	}

	// Update and Upload Constants
	{
		GPU_TIMING_SCOPE("Upload", command_list, &gStats.mGPUTimingMS.mUpload);

		// Update
		{
			// https://google.github.io/filament/Filament.html#imagingpipeline/physicallybasedcamera/exposurevalue
			gConstants.mEV100			= glm::log2(
											(gCameraSettings.mExposureControl.mAperture * gCameraSettings.mExposureControl.mAperture) / 
											(1.0f / gCameraSettings.mExposureControl.mInvShutterSpeed) * 100.0f / gCameraSettings.mExposureControl.mSensitivity);
			gConstants.mSunDirection	= glm::vec4(0,1,0,0) * glm::rotate(gConstants.mSunZenith, glm::vec3(0, 0, 1)) * glm::rotate(gConstants.mSunAzimuth + glm::pi<float>() / 2.0f, glm::vec3(0, 1, 0));
			gConstants.mLightCount		= (glm::uint)gScene.GetSceneContent().mLights.size();

			if (!gHeadless && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				gConstants.mPixelDebugCoord = glm::uvec2(static_cast<uint32_t>(ImGui::GetMousePos().x), (uint32_t)ImGui::GetMousePos().y);
		}

		// Upload
		{
			static Constants sConstantsCopy			= gConstants;

			if (sConstantsCopy.mPixelDebugCoord != gConstants.mPixelDebugCoord)
				gConstants.mDebugFlag |= DebugFlag::UpdateRayInspection;

			// Whitelist to ignore for accumulation reset
			sConstantsCopy.mFrameIndex				= gConstants.mFrameIndex;
			sConstantsCopy.mTime					= gConstants.mTime;
			sConstantsCopy.mCurrentFrameIndex		= gConstants.mCurrentFrameIndex;
			sConstantsCopy.mCurrentFrameWeight		= gConstants.mCurrentFrameWeight;
			sConstantsCopy.mPixelDebugCoord			= gConstants.mPixelDebugCoord;
			sConstantsCopy.mDebugMode				= gConstants.mDebugMode;
			sConstantsCopy.mDebugFlag				= gConstants.mDebugFlag;
			sConstantsCopy.mSpatialCache			= gConstants.mSpatialCache;
			sConstantsCopy.mReSTIR					= gConstants.mReSTIR;
			sConstantsCopy.mBRDFExplorer			= gConstants.mBRDFExplorer;

			if (memcmp(&sConstantsCopy, &gConstants, sizeof(Constants)) != 0)
				gRenderer.mAccumulationResetRequested = true;

			{
				if (gRenderer.mAccumulationResetRequested)
					gConstants.mCurrentFrameIndex = 0;

				int accumulation_frame_count = gRenderer.mAccumulationFrameUnlimited ? INT_MAX : gRenderer.mAccumulationFrameCount;
				bool accumulation_done = gConstants.mCurrentFrameIndex + 1 > accumulation_frame_count;
				gConstants.mCurrentFrameIndex = gMin(gConstants.mCurrentFrameIndex, accumulation_frame_count - 1);

				if (accumulation_done || gRenderer.mAccumulationPaused)
					gConstants.mCurrentFrameWeight = 0.0f;
				else
					gConstants.mCurrentFrameWeight = 1.0f / (gConstants.mCurrentFrameIndex + 1);
			}

			// ReSTIR
			{
				gConstants.mReSTIR.mTemporalCounter++;
			}
			
			sConstantsCopy = gConstants;
			gRenderer.mAccumulationResetRequested = false;

			memcpy(gRenderer.mRuntime.mConstantsBuffer.mUploadPointer[gGetFrameContextIndex()], &gConstants, sizeof(gConstants));
		}

		// Reset
		{
			gConstants.mDebugFlag &= ~DebugFlag::UpdateRayInspection;

			if (gRenderer.mSpatialCacheActiveOnce)
			{
				gConstants.mSpatialCache.mFrameActive = false;
				gRenderer.mSpatialCacheActiveOnce = false;
			}
		}

		gBarrierTransition(gCommandList, gRenderer.mRuntime.mConstantsBuffer.mResource.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
		gCommandList->CopyResource(gRenderer.mRuntime.mConstantsBuffer.mResource.Get(), gRenderer.mRuntime.mConstantsBuffer.mUploadResource[gGetFrameContextIndex()].Get());
		gBarrierTransition(gCommandList, gRenderer.mRuntime.mConstantsBuffer.mResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	}

	// Renderer
	{
		GPU_TIMING_SCOPE("Renderer", command_list, &gStats.mGPUTimingMS.mRenderer);

		gRenderer.Render(command_list);
	}

	// Scene
	{
		GPU_TIMING_SCOPE("Scene", command_list, &gStats.mGPUTimingMS.mScene);

		gScene.Render(command_list);
	}

	// Atmosphere
	{
		GPU_TIMING_SCOPE("Atmosphere", command_list, &gStats.mGPUTimingMS.mAtmosphere);

		gAtmosphere.Render(command_list);
	}

	// Cloud
	{
		GPU_TIMING_SCOPE("Cloud", command_list, &gStats.mGPUTimingMS.mCloud);

		gCloud.Render(command_list);
	}

	// Texture Generator
	{
		GPU_TIMING_SCOPE("TextureGenerator", command_list, &gStats.mGPUTimingMS.mTextureGenerator);

		gRenderer.Setup(gRenderer.mRuntime.mGenerateTextureShader);

		BarrierScope scope(command_list, gRenderer.mRuntime.mGenerateTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		command_list->Dispatch(gAlignUpDiv(gRenderer.mRuntime.mGenerateTexture.mWidth, 8u), gAlignUpDiv(gRenderer.mRuntime.mGenerateTexture.mHeight, 8u), 1);

		gBarrierUAV(command_list, nullptr);
	}

	// BRDF Slice
	if (!gHeadless)
	{
		GPU_TIMING_SCOPE("BRDFSlice", command_list, &gStats.mGPUTimingMS.mBRDFSlice);

		gRenderer.Setup(gRenderer.mRuntime.mBRDFSliceShader);

		BarrierScope scope(command_list, gRenderer.mRuntime.mBRDFSliceTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		command_list->Dispatch(gAlignUpDiv(gRenderer.mRuntime.mBRDFSliceTexture.mWidth, 8u), gAlignUpDiv(gRenderer.mRuntime.mBRDFSliceTexture.mHeight, 8u), 1);

		gBarrierUAV(command_list, nullptr);
	}

	// Clear for debug
	if (!gHeadless)
	{
		GPU_TIMING_SCOPE("Clear", command_list, &gStats.mGPUTimingMS.mClear);

		gRenderer.Setup(gRenderer.mRuntime.mClearShader);
		command_list->Dispatch(gAlignUpDiv(PixelInspection::kArraySize, 64u), 1, 1);

		BarrierScope depth_scope(command_list, gRenderer.mRuntime.mScreenDebugTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gRenderer.ClearUnorderedAccessViewFloat(gRenderer.mRuntime.mScreenDebugTexture);

		gBarrierUAV(command_list, nullptr);
	}
	
	// Depth
	if (!gHeadless)
	{
		GPU_TIMING_SCOPE("Depths", command_list, &gStats.mGPUTimingMS.mDepths);

		BarrierScope depth_scope(command_list, gRenderer.mRuntime.mScreenDepthTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		D3D12_VIEWPORT viewport =
		{
			.TopLeftX = 0.0f,
			.TopLeftY = 0.0f,
			.Width = static_cast<float>(gRenderer.mScreenSize.x),
			.Height = static_cast<float>(gRenderer.mScreenSize.y),
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f,
		};
		command_list->RSSetViewports(1, &viewport);
		D3D12_RECT rect =
		{
			.left = 0,
			.top = 0,
			.right = static_cast<LONG>(gRenderer.mScreenSize.x),
			.bottom = static_cast<LONG>(gRenderer.mScreenSize.y),
		};
		command_list->RSSetScissorRects(1, &rect);
		D3D12_CPU_DESCRIPTOR_HANDLE depth_cpu_handle = gCPUContext.mDSVDescriptorHeap.GetCPUHandle(gRenderer.mRuntime.mScreenDepthTexture.mDSVIndex);
		command_list->OMSetRenderTargets(0, nullptr, false, &depth_cpu_handle);
		command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		gRenderer.Setup(gRenderer.mRuntime.mDepthShader);
		command_list->DrawInstanced(3, 1, 0, 0);

		gBarrierUAV(command_list, nullptr);
	}

	// PrepareLights
	if (gScene.GetSceneContent().mEmissiveTriangleCount > 0)
	{
		GPU_TIMING_SCOPE("PrepareLights", command_list, &gStats.mGPUTimingMS.mPrepareLights);

		gRenderer.Setup(gRenderer.mRuntime.mPrepareLightsShader, { .mData0 = { gScene.GetPrepareLightsTaskCount(), 0, 0, 0 } });
		command_list->Dispatch(gAlignUpDiv(gScene.GetSceneContent().mEmissiveTriangleCount, 256u), 1, 1);

		gBarrierUAV(command_list, nullptr);
	}

	// RayQuery
	{
		GPU_TIMING_SCOPE("RayQuery", command_list, &gStats.mGPUTimingMS.mRayQuery);

		gRenderer.Setup(gRenderer.mRuntime.mRayQueryShader);
		command_list->Dispatch(gAlignUpDiv(gRenderer.mScreenSize.x, 8u), gAlignUpDiv(gRenderer.mScreenSize.y, 8u), 1);

		gBarrierUAV(command_list, nullptr);
	}

	// Test Hit Shader
	if (gConfigs.mTestHitShader)
	{
		GPU_TIMING_SCOPE("HitShader", command_list, &gStats.mGPUTimingMS.mHitShader);

		D3D12_DISPATCH_RAYS_DESC dispatch_rays_desc = {};
		{
			dispatch_rays_desc.Width = gRenderer.mScreenSize.x;
			dispatch_rays_desc.Height = gRenderer.mScreenSize.y;
			dispatch_rays_desc.Depth = 1;

			// RayGen
			dispatch_rays_desc.RayGenerationShaderRecord.StartAddress = gRenderer.mRuntime.mLibShaderTable.mResource->GetGPUVirtualAddress() + gRenderer.mRuntime.mLibShaderTable.mEntrySize * gRenderer.mRuntime.mLibShaderTable.mRayGenOffset;
			dispatch_rays_desc.RayGenerationShaderRecord.SizeInBytes = gRenderer.mRuntime.mLibShaderTable.mEntrySize;

			// Miss
			dispatch_rays_desc.MissShaderTable.StartAddress = gRenderer.mRuntime.mLibShaderTable.mResource->GetGPUVirtualAddress() + gRenderer.mRuntime.mLibShaderTable.mEntrySize * gRenderer.mRuntime.mLibShaderTable.mMissOffset;
			dispatch_rays_desc.MissShaderTable.StrideInBytes = gRenderer.mRuntime.mLibShaderTable.mEntrySize;
			dispatch_rays_desc.MissShaderTable.SizeInBytes = gRenderer.mRuntime.mLibShaderTable.mEntrySize * gRenderer.mRuntime.mLibShaderTable.mMissCount;

			// HitGroup
			dispatch_rays_desc.HitGroupTable.StartAddress = gRenderer.mRuntime.mLibShaderTable.mResource->GetGPUVirtualAddress() + gRenderer.mRuntime.mLibShaderTable.mEntrySize * gRenderer.mRuntime.mLibShaderTable.mHitGroupOffset;
			dispatch_rays_desc.HitGroupTable.StrideInBytes = gRenderer.mRuntime.mLibShaderTable.mEntrySize;
			dispatch_rays_desc.HitGroupTable.SizeInBytes = gRenderer.mRuntime.mLibShaderTable.mEntrySize * gRenderer.mRuntime.mLibShaderTable.mHitGroupCount;
		}

		gRenderer.Setup(gRenderer.mRuntime.mLibShader);
		command_list->DispatchRays(&dispatch_rays_desc);

		gBarrierUAV(command_list, nullptr);
	}

	if (gConfigs.mTestSlangShader)
	{
		GPU_TIMING_SCOPE("Slang", command_list, &gStats.mGPUTimingMS.mSlangShader);

		gRenderer.Setup(gRenderer.mRuntime.mSlangShader);
		command_list->Dispatch(gAlignUpDiv(gRenderer.mScreenSize.x, 8u), gAlignUpDiv(gRenderer.mScreenSize.y, 8u), 1);

		gBarrierUAV(command_list, nullptr);
	}

	// Composite
	if (!gHeadless)
	{
		GPU_TIMING_SCOPE("Composite", command_list, &gStats.mGPUTimingMS.mComposite);

		gBarrierTransition(gCommandList, back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		BarrierScope depth_scope(gCommandList, gRenderer.mRuntime.mScreenDepthTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_READ);

		D3D12_VIEWPORT viewport =
		{
			.TopLeftX = 0.0f,
			.TopLeftY = 0.0f,
			.Width = static_cast<float>(gRenderer.mScreenSize.x),
			.Height = static_cast<float>(gRenderer.mScreenSize.y),
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f,
		};
		gCommandList->RSSetViewports(1, &viewport);
		D3D12_RECT rect =
		{
			.left = 0,
			.top = 0,
			.right = static_cast<LONG>(gRenderer.mScreenSize.x),
			.bottom = static_cast<LONG>(gRenderer.mScreenSize.y),
		};
		gCommandList->RSSetScissorRects(1, &rect);
		D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_cpu_handle = gCPUContext.mRTVDescriptorHeap.GetCPUHandle(gRenderer.mRuntime.mBackBuffers[back_buffer_index].mRTVIndex);
		D3D12_CPU_DESCRIPTOR_HANDLE depth_cpu_handle = gCPUContext.mDSVDescriptorHeap.GetCPUHandle(gRenderer.mRuntime.mScreenDepthTexture.mDSVIndex);
		gCommandList->OMSetRenderTargets(1, &back_buffer_cpu_handle, false, &depth_cpu_handle);

		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gRenderer.Setup(gRenderer.mRuntime.mCompositeShader);
		gCommandList->DrawInstanced(3, 1, 0, 0);

		// Line
		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		gRenderer.Setup(gRenderer.mRuntime.mLineShader);
		gCommandList->DrawInstanced(RayInspection::kArraySize * 3 /* Position, Normal, LightPosition */ * 2 /* 2 vertex per line */, 1, 0, 0);

		// Line Hidden
		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		gRenderer.Setup(gRenderer.mRuntime.mLineHiddenShader);
		gCommandList->DrawInstanced(RayInspection::kArraySize * 3 /* Position, Normal, LightPosition */ * 2 /* 2 vertex per line */, 1, 0, 0);
	}

	// Readback Sequence
	bool sequence_recording = gRenderer.mSequenceFrameRecording >= 0 && gConstants.mCurrentFrameIndex + 1 == gRenderer.mAccumulationFrameCount;
	bool sequence_dump_png = gRenderer.mSequenceDumpPNG; // [NOTE] This flag is updated by UI (sPrepareImGui) below, state from last frame
	if (sequence_recording || sequence_dump_png)
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Readback SceneColor with postprocess for Sequence");

		gRenderer.Setup(gRenderer.mRuntime.mReadbackShader);
		gCommandList->Dispatch(gAlignUpDiv(gRenderer.mScreenSize.x, 8u), gAlignUpDiv(gRenderer.mScreenSize.y, 8u), 1);
	}

	// Readback
	if (!gHeadless)
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Readback");

		for (auto&& buffer : gRenderer.mRuntime.mBuffers)
			buffer.Readback(command_list);
	}

	// Draw ImGui
	if (!gHeadless)
	{
		GPU_TIMING_SCOPE("ImGui", command_list, &gStats.mGPUTimingMS.mImGui);

		gPrepareImGui(); // Keep this right before render to get latest data

		ImGui::Render();

		D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_cpu_handle = gCPUContext.mRTVDescriptorHeap.GetCPUHandle(gRenderer.mRuntime.mBackBuffers[back_buffer_index].mRTVIndex);
		gCommandList->OMSetRenderTargets(1, &back_buffer_cpu_handle, false, nullptr);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);
	}

	// Frame End
	{
		if (!gHeadless)
			gBarrierTransition(gCommandList, back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		gGPUTiming.FrameEnd(command_list, gRenderer.mRuntime.mQueryBuffer.mReadbackResource[gGetFrameContextIndex()].Get());
		gCommandList->Close();
		gCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&gCommandList));
	}

	// Dump Texture
	{
		if (gCPUContext.mDumpTextureRef != nullptr && gCPUContext.mDumpTextureRef->mResource != nullptr)
		{
			CPUTimingScope timing_scope;
			timing_scope.mTraceName = "DumpTexture";

			DirectX::ScratchImage image;
			DirectX::CaptureTexture(gCommandQueue, gCPUContext.mDumpTextureRef->mResource.Get(), false, image, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);

			std::filesystem::path path = gEnsureDumpDirectoryExists();
			path += gCPUContext.mDumpTextureRef->mName;
			path += ".dds";
			DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, path.c_str());

			gCPUContext.mDumpTextureRef = nullptr;
		}
	}

	// Dump Texture for Sequence
	// [NOTE] Don't use global state here, those are updated by UI above. Otherwise readback is not done for current frame due to execution order
	if (sequence_recording || sequence_dump_png)
	{
		DirectX::ScratchImage image;
		DirectX::CaptureTexture(gCommandQueue, gRenderer.mRuntime.mScreenReadbackTexture.mResource.Get(), false, image, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);

		wchar_t filename[256];
		swprintf_s(filename, L"%03u.png", gRenderer.mSequenceFrameRecording);
		DirectX::SaveToWICFile(image.GetImages(), image.GetImageCount(), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, filename);

		if (sequence_recording)
		{
			gConstants.mSequenceFrameIndex++;
			gRenderer.mSequenceFrameRecording++;

			if (gRenderer.mSequenceFrameRecording == gConstants.mSequenceFrameCount)
			{
				gConstants.mSequenceEnabled = 0;
				gConstants.mSequenceFrameIndex = 0;
				gRenderer.mSequenceFrameRecording = -1;
				if (gHeadless)
					gHeadlessDone = true;
			}
		}

		if (sequence_dump_png)
			gRenderer.mSequenceDumpPNG = false;
	}

	// Present
	if (!gHeadless)
	{
		if (gDisplaySettings.mVsync)
			gSwapChain->Present(1, 0);
		else
			gSwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}

	// Finish Frame
	{
		UINT64 signal_value = ++gFenceLastSignaledValue;
		gCommandQueue->Signal(gIncrementalFence, signal_value);
		frame_context.mFenceValue = signal_value;

		if (!gRenderer.mAccumulationPaused)
			gConstants.mCurrentFrameIndex++;

		if (!gHeadless) // [TODO] Calculate DeltaTime without ImGui
			gConstants.mTime += ImGui::GetIO().DeltaTime;
	}
}

static void sMessageCallback(D3D12_MESSAGE_CATEGORY inCategory, D3D12_MESSAGE_SEVERITY inSeverity, D3D12_MESSAGE_ID inID, LPCSTR inDescription, void* inContext)
{
	(void)inContext;
	std::string message = std::format("{}\n\tD3D12_MESSAGE_CATEGORY = {}\n\tD3D12_MESSAGE_SEVERITY = {}\n\tD3D12_MESSAGE_ID = {}\n", 
		inDescription,
		nameof::nameof_enum(inCategory), 
		nameof::nameof_enum(inSeverity), 
		nameof::nameof_enum(inID)); // Note NAMEOF_ENUM_RANGE_MAX is not large enough for this
	gTrace(message);

	if (inSeverity < D3D12_MESSAGE_SEVERITY_INFO)
		gAssert(false);
}

// Helper functions
static bool sCreateDeviceD3D(HWND hWnd)
{
	gValidate(CoInitialize(NULL)); // Required by WICFactory

	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	{
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = kFrameInFlightCount;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = kBackBufferFormat;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Stereo = FALSE;
	}

	bool enable_debug_layer = DX12_ENABLE_DEBUG_LAYER;
	if (GetModuleHandleA("Nvda.Graphics.Interception.dll") != NULL)
		enable_debug_layer = false;
	if (enable_debug_layer)
	{
		ComPtr<ID3D12Debug> dx12Debug = nullptr;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12Debug))))
			dx12Debug->EnableDebugLayer();

		if (DX12_ENABLE_GBV)
		{
			ComPtr<ID3D12Debug1> dx12Debug1 = nullptr;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12Debug1))))
				dx12Debug1->SetEnableGPUBasedValidation(true);
		}
	}

	// Create device with highest feature level as possible
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_12_2;
	if (D3D12CreateDevice(nullptr, feature_level, IID_PPV_ARGS(&gDevice)) != S_OK)
		return false;

	// Check SM
	D3D12_FEATURE_DATA_SHADER_MODEL shader_model = { D3D_SHADER_MODEL_6_9 };
	if (FAILED(gDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
		|| (shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_9))
		return false;

	// Check DXR, see https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	if (FAILED(gDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))
		|| options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
		return false;

	// Check EnhancedBarriers, see https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html
	D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
	if (FAILED(gDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)))
		|| !options12.EnhancedBarriersSupported)
		return false;

	// NVAPI, based on RTXDI, RTXCR. NvAPI_Unload is not used.
	if (gConfigs.mUseNVAPI)
		gNVAPI.mInitialized = NvAPI_Initialize() == NVAPI_OK;
	if (gNVAPI.mInitialized)
	{
		NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAPS caps = NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_NONE;
		gVerify(NvAPI_D3D12_GetRaytracingCaps(gDevice, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_OPACITY_MICROMAP, &caps, sizeof(caps)) == NVAPI_OK);
		gNVAPI.mMicromapSupported = caps == NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_STANDARD;

		NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAPS clusterCaps = NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_NONE;
		gVerify(NvAPI_D3D12_GetRaytracingCaps(gDevice, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS, &clusterCaps, sizeof(clusterCaps)) == NVAPI_OK);
		gNVAPI.mClusterSupported = clusterCaps == NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_STANDARD;
		
		NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAPS lss_caps = NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_NONE;
		gVerify(NvAPI_D3D12_GetRaytracingCaps(gDevice, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_LINEAR_SWEPT_SPHERES, &lss_caps, sizeof(NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAPS)) == NVAPI_OK);
		gNVAPI.mLinearSweptSpheresSupported = lss_caps == NVAPI_D3D12_RAYTRACING_LINEAR_SWEPT_SPHERES_CAP_STANDARD;

		NVAPI_D3D12_RAYTRACING_SPHERES_CAPS sphere_caps = NVAPI_D3D12_RAYTRACING_SPHERES_CAP_NONE;
		gVerify(NvAPI_D3D12_GetRaytracingCaps(gDevice, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_SPHERES, &sphere_caps, sizeof(NVAPI_D3D12_RAYTRACING_SPHERES_CAPS)) == NVAPI_OK);
		gNVAPI.mSpheresSupported = sphere_caps == NVAPI_D3D12_RAYTRACING_SPHERES_CAP_STANDARD;

		// Assume both LSS and sphere or neither. Will only check for LSS after this line
		gVerify(gNVAPI.mLinearSweptSpheresSupported == gNVAPI.mSpheresSupported);

		if (gNVAPI.mMicromapSupported || gNVAPI.mClusterSupported || gNVAPI.mLinearSweptSpheresSupported || gNVAPI.mSpheresSupported)
		{
			NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS params = {};
			params.version = NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;
			params.flags = 0;
			params.flags |= (gNVAPI.mMicromapSupported ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_OMM_SUPPORT : 0);
			params.flags |= (gNVAPI.mClusterSupported ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT : 0);
			params.flags |= (gNVAPI.mLinearSweptSpheresSupported ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_LSS_SUPPORT : 0);
			params.flags |= (gNVAPI.mSpheresSupported ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_SPHERE_SUPPORT : 0);
			gVerify(NvAPI_D3D12_SetCreatePipelineStateOptions(gDevice, &params) == NVAPI_OK);
		}

		// Seems this replaced NV_EXTN_OP_HIT_OBJECT_REORDER_THREAD
		NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAPS ser = NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_NONE;
		gVerify(NvAPI_D3D12_GetRaytracingCaps(gDevice, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_THREAD_REORDERING, &ser, sizeof(ser)) == NVAPI_OK);
		gNVAPI.mShaderExecutionReorderingSupported = ser == NVAPI_D3D12_RAYTRACING_THREAD_REORDERING_CAP_STANDARD;

		if (gNVAPI.mShaderExecutionReorderingSupported)
		{
			gVerify(NvAPI_D3D12_SetNvShaderExtnSlotSpace(gDevice, NV_SHADER_EXTN_SLOT, NV_SHADER_EXTN_REGISTER_SPACE) == NVAPI_OK);
			gNVAPI.mFakeUAVEnabled = true;
		}
	}

	// InfoQueue callback
	if (DX12_ENABLE_INFO_QUEUE_CALLBACK)
	{
		ComPtr<ID3D12InfoQueue1> info_queue;
		if (SUCCEEDED(gDevice->QueryInterface(IID_PPV_ARGS(&info_queue))))
		{
			DWORD cookie = 0;

			if (FAILED(info_queue->RegisterMessageCallback(sMessageCallback, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS, nullptr, &cookie)))
				return false;
		}
	}

	// CommandQueue
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		gValidate(gDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&gCommandQueue)));
		gCommandQueue->SetName(L"gCommandQueue");
		gCommandQueue->GetTimestampFrequency(&gGPUTiming.mTimestampFrequency);
	}

	// FrameContext
	for (glm::uint i = 0; i < kFrameInFlightCount; i++)
	{
		std::wstring name;

		// Allocator
		{
			gValidate(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gFrameContexts[i].mCommandAllocator)));
			name = L"FrameContext.CommandAllocator_" + std::to_wstring(i);
			gFrameContexts[i].mCommandAllocator->SetName(name.c_str());
		}

		// DescriptorHeap
		{
			gFrameContexts[i].mViewDescriptorHeap.Initialize();
			name = L"FrameContext.DescriptorHeap_" + std::to_wstring(i);
			gFrameContexts[i].mViewDescriptorHeap.mHeap->SetName(name.c_str());

			gFrameContexts[i].mSamplerDescriptorHeap.Initialize();
			name = L"FrameContext.SamplerDescriptorHeap_" + std::to_wstring(i);
			gFrameContexts[i].mSamplerDescriptorHeap.mHeap->SetName(name.c_str());

			gFrameContexts[i].mClearDescriptorHeap.Initialize();
			name = L"FrameContext.ClearDescriptorHeap_" + std::to_wstring(i);
			gFrameContexts[i].mClearDescriptorHeap.mHeap->SetName(name.c_str());

			D3D12_SAMPLER_DESC sampler_descs[] =
			{
				{.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP, .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP, .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP, .MipLODBias = 0, .MaxAnisotropy = 0, .ComparisonFunc = D3D12_COMPARISON_FUNC_NONE, .BorderColor = {0,0,0,0}, .MinLOD = 0, .MaxLOD = D3D12_FLOAT32_MAX },
				{.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .MipLODBias = 0, .MaxAnisotropy = 0, .ComparisonFunc = D3D12_COMPARISON_FUNC_NONE, .BorderColor = {0,0,0,0}, .MinLOD = 0, .MaxLOD = D3D12_FLOAT32_MAX },
				{.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .MipLODBias = 0, .MaxAnisotropy = 0, .ComparisonFunc = D3D12_COMPARISON_FUNC_NONE, .BorderColor = {0,0,0,0}, .MinLOD = 0, .MaxLOD = D3D12_FLOAT32_MAX },
				{.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .MipLODBias = 0, .MaxAnisotropy = 0, .ComparisonFunc = D3D12_COMPARISON_FUNC_NONE, .BorderColor = {0,0,0,0}, .MinLOD = 0, .MaxLOD = D3D12_FLOAT32_MAX },
			};
			static_assert(gArraySize(sampler_descs) == (int)SamplerDescriptorIndex::Count);
			for (int sampler_index = 0; sampler_index < (int)SamplerDescriptorIndex::Count; sampler_index++)
				gDevice->CreateSampler(&sampler_descs[sampler_index], gFrameContexts[i].mSamplerDescriptorHeap.GetCPUHandle((SamplerDescriptorIndex)sampler_index));
		}
	}

	// CPUContext
	{
		std::wstring name;

		gCPUContext.mRTVDescriptorHeap.Initialize();
		name = L"CPUContext.RTVDescriptorHeap";
		gCPUContext.mRTVDescriptorHeap.mHeap->SetName(name.c_str());

		gCPUContext.mDSVDescriptorHeap.Initialize();
		name = L"CPUContext.DSVDescriptorHeap";
		gCPUContext.mDSVDescriptorHeap.mHeap->SetName(name.c_str());
	}

	gValidate(gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gFrameContexts[0].mCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&gCommandList)));
	gCommandList->SetName(L"gCommandList");

	D3D12_QUERY_HEAP_DESC query_heap_desc = { .Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = kTimestampCount };
	gValidate(gDevice->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&gQueryHeap)));
	gQueryHeap->SetName(L"gQueryHeap");

	gValidate(gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gIncrementalFence)));
	gIncrementalFence->SetName(L"gIncrementalFence");

	gIncrementalFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (gIncrementalFenceEvent == nullptr)
		return false;

	if (!gHeadless)
	{
		ComPtr<IDXGIFactory4> dxgi_factory = nullptr;
		ComPtr<IDXGISwapChain1> swap_chain = nullptr;

		UINT flags = 0;
		if (DX12_ENABLE_DEBUG_LAYER)
			flags = DXGI_CREATE_FACTORY_DEBUG;

		if (CreateDXGIFactory2(flags, IID_PPV_ARGS(&dxgi_factory)) != S_OK)
			return false;
		
		if (dxgi_factory->CreateSwapChainForHwnd(gCommandQueue, hWnd, &sd, nullptr, nullptr, &swap_chain) != S_OK)
			return false;

		if (swap_chain->QueryInterface(IID_PPV_ARGS(&gSwapChain)) != S_OK)
			return false;

		// Fullscreen -> Windowed cause crash on resource reference in WM_SIZE handling, disable fullscreen for now
		dxgi_factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

		gSwapChain->SetMaximumFrameLatency(kFrameInFlightCount);
		gSwapChainWaitableObject = gSwapChain->GetFrameLatencyWaitableObject();
	}

	return true;
}

static void sCleanupDeviceD3D()
{
	if (gNVAPI.mInitialized)
		NvAPI_Unload();

	gSafeRelease(gSwapChain);
	gSafeCloseHandle(gSwapChainWaitableObject);

	for (UINT i = 0; i < kFrameInFlightCount; i++)
		gFrameContexts[i].Reset();

	gCPUContext.Reset();

	gSafeRelease(gDevice);
	gSafeRelease(gCommandQueue);
	gSafeRelease(gCommandList);
	gSafeRelease(gRTVDescriptorHeap);

	gSafeRelease(gQueryHeap);

	gSafeRelease(gIncrementalFence);
	gSafeCloseHandle(gIncrementalFenceEvent);

	if (DX12_ENABLE_DEBUG_LAYER)
	{
		ComPtr<IDXGIDebug1> dxgi_debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
			dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
	}

	CoUninitialize();
}

static void sWaitForGPU()
{
	if (gFenceLastSignaledValue == 0) // Nothing has been done yet, and DirectX complains about waiting on 0
		return;

	gIncrementalFence->SetEventOnCompletion(gFenceLastSignaledValue, gIncrementalFenceEvent);
	WaitForSingleObject(gIncrementalFenceEvent, INFINITE);
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
			sWaitForGPU();
			gRenderer.FinalizeScreenSizeTextures();

			gDisplaySettings.mWindowSize.x = gMax((UINT)LOWORD(lParam), 8u);
			gDisplaySettings.mWindowSize.y = gMax((UINT)HIWORD(lParam), 8u);
			DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
			gSwapChain->GetDesc1(&swap_chain_desc);
			gSwapChain->ResizeBuffers(
				swap_chain_desc.BufferCount,
				gDisplaySettings.mWindowSize.x,
				gDisplaySettings.mWindowSize.y,
				swap_chain_desc.Format,
				swap_chain_desc.Flags);

			gRenderer.InitializeScreenSizeTextures();
		}
		return 0;
	case WM_DPICHANGED:
		{
			UINT dpi = HIWORD(wParam);
			float scale = dpi * 1.0f / USER_DEFAULT_SCREEN_DPI;
			ImGui::GetStyle().ScaleAllSizes(scale / ImGui::gDpiScale);
			ImGui::gDpiScale = scale;

			// https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged
			// https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows
			// 1. Ensure that the mouse cursor will stay in the same relative position on the Window when dragging between displays
			// 2. Prevent the application window from getting into a recursive dpi - change cycle where one DPI change triggers a subsequent DPI change, which triggers yet another DPI change.
			RECT* const prcNewWindow = (RECT*)lParam;
			SetWindowPos(hWnd,
				NULL,
				prcNewWindow->left,
				prcNewWindow->top,
				prcNewWindow->right - prcNewWindow->left,
				prcNewWindow->bottom - prcNewWindow->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		if (!gHeadless)
		{
			// INI
			auto SetPrivateProfileInt = [](LPCWSTR lpAppName, LPCWSTR lpKeyName, INT nValue, LPCWSTR lpFileName)
			{
				wchar_t buffer[16];
				_itow_s(nValue, buffer, std::size(buffer), 10);
				return WritePrivateProfileString(lpAppName, lpKeyName, buffer, lpFileName) != 0;
			};
			RECT rect_for_ini;
			::GetWindowRect(hWnd, &rect_for_ini);
			SetPrivateProfileInt(L"Main", L"Window_X", rect_for_ini.left, kINIPathW);
			SetPrivateProfileInt(L"Main", L"Window_Y", rect_for_ini.top, kINIPathW);
			WritePrivateProfileString(NULL, NULL, NULL, kINIPathW); // Flush
		}		
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
