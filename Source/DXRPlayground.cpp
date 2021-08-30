#include "Common.h"

#include "Thirdparty/filewatch/FileWatch.hpp"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"

#include "Scene.h"
#include "RayTrace.h"

#include "Atmosphere.h"
#include "Cloud.h"
#include "DDGI.h"

// Use Agility SDK
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 4; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

#define DX12_ENABLE_DEBUG_LAYER			(1)

static const wchar_t*					kApplicationTitleW = L"DXR Playground";

enum class ScenePresetType
{
	None,

	CornellBox,
	VeachMIS,
	Furnance,
	PrecomputedAtmosphere,
	PrecomputedAtmosphere_Artifact_Mu,

	// Extra (use download_extra_asset.ps1 to download)
	Rungholt,

	Hillaire20,

	COUNT,
};

struct ScenePreset
{
	const char* mName;
	const char* mPath;
	glm::vec4 mCameraPosition;
	glm::vec4 mCameraDirection;
	float mHorizontalFovDegree;
	glm::mat4x4 mTransform;
	float mSunAzimuth;
	float mSunZenith;
};

static ScenePreset kScenePresets[(int)ScenePresetType::COUNT] =
{
	{ "None",									nullptr,																			glm::vec4(0.0f, 1.0f, 3.0f, 0.0f),			glm::vec4(0.0f, 0.0f, -1.0f, 0.0f),		90.0f, glm::mat4x4(1.0f),											0.0f, glm::pi<float>() / 4.0f,},
	{ "CornellBox",								"Asset/raytracing-references/cornellbox/cornellbox.obj",							glm::vec4(0.0f, 1.0f, 3.0f, 0.0f),			glm::vec4(0.0f, 0.0f, -1.0f, 0.0f),		90.0f, glm::mat4x4(1.0f),											0.0f, glm::pi<float>() / 4.0f,},
	{ "VeachMIS",								"Asset/raytracing-references/veach-mis/veach-mis.obj",								glm::vec4(0.0f, 1.0f, 13.0f, 0.0f),			glm::vec4(0.0f, 0.0f, -1.0f, 0.0f),		90.0f, glm::mat4x4(1.0f),											0.0f, glm::pi<float>() / 4.0f,},
	{ "Furnance",								"Asset/raytracing-references/furnace-light-sampling/furnace-light-sampling.obj",	glm::vec4(0.0f, 1.0f, 13.0f, 0.0f),			glm::vec4(0.0f, 0.0f, -1.0f, 0.0f),		90.0f, glm::mat4x4(1.0f),											0.0f, glm::pi<float>() / 4.0f,},
	{ "PrecomputedAtmosphere",					"Asset/primitives/sphere.obj",														glm::vec4(0.0f, 0.0f, 9.0f, 0.0f),			glm::vec4(0.0f, 0.0f, -1.0f, 0.0f),		90.0f, glm::translate(glm::vec3(0.0f, 1.0f, 0.0f)),					0.0f, glm::pi<float>() / 4.0f,},
	{ "PrecomputedAtmosphere_Artifact_Mu",		"Asset/primitives/sphere.obj",														glm::vec4(0.0f, 80.0f, 150.0f, 0.0f),		glm::vec4(0.0f, 0.0f, -1.0f, 0.0f),		90.0f, glm::scale(glm::vec3(100.0f, 100.0f, 100.0f)),				0.0f, glm::pi<float>() / 4.0f,},

	{ "Rungholt",								"Asset/extra/rungholt/rungholt.obj",												glm::vec4(-234.0f, 88.0f, 98.0f, 0.0f),		glm::vec4(0.88f, -0.15f, -0.45f, 0.0f), 90.0f, glm::mat4x4(1.0f),											0.0f, glm::pi<float>() / 4.0f,},

	{ "Hillaire20",								nullptr,																			glm::vec4(0.0f, 0.5, -1.0f, 0.0f),			glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),		98.8514328f, glm::translate(glm::vec3(0.0f, 1.0f, 0.0f)),		0.0f, glm::pi<float>() / 2.0f - 0.45f,},
};
static ScenePresetType sCurrentScene = ScenePresetType::Hillaire20;
static ScenePresetType sPreviousScene = ScenePresetType::Hillaire20;

static bool sReloadRequested = false;

struct CameraSettings
{
	glm::vec2		mMoveRotateSpeed = glm::vec2(0.1f, 0.01f);
	float			mHorizontalFovDegree = 90.0f;

	struct ExposureControl
	{
		// Sunny 16 rule
		float		mAperture = 16.0;						// N, f-stops
		float		mInvShutterSpeed = 100.0;				// t, seconds
		float		mSensitivity = 100.0f;					// S, ISO
	};
	ExposureControl mExposureControl;
	void			ResetExposure()		{ mExposureControl = ExposureControl(); }
};
CameraSettings		gCameraSettings = {};

struct DisplaySettings
{
	glm::ivec2		mRenderResolution	= glm::ivec2(0, 0);
	bool			mVsync				= true;
};
DisplaySettings		gDisplaySettings	= {};

// Forward declarations of helper functions
static bool sCreateDeviceD3D(HWND hWnd);
static void sCleanupDeviceD3D();
static void sCreateRenderTarget();
static void sCleanupRenderTarget();
static void sWaitForIdle();
static void sWaitForLastSubmittedFrame();
static FrameContext* sWaitForNextFrameResources();
static void sUpdate();
static void sRender();
static LRESULT WINAPI sWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void sUpdate()
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

		if (mouse_delta.x != 0 || mouse_delta.y != 0) // otherwise result of glm::normalize might oscillate
		{
			glm::vec4 front = gPerFrameConstantBuffer.mCameraDirection;
			glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(front), glm::vec3(0, 1, 0))), 0);
			glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(right), glm::vec3(front))), 0);

			front = glm::rotate(-mouse_delta.x * gCameraSettings.mMoveRotateSpeed.y, glm::vec3(up)) * front;
			front = glm::rotate(-mouse_delta.y * gCameraSettings.mMoveRotateSpeed.y, glm::vec3(right)) * front;

			gPerFrameConstantBuffer.mCameraDirection = glm::normalize(front);
		}

		if (glm::isnan(gPerFrameConstantBuffer.mCameraDirection.x))
			gPerFrameConstantBuffer.mCameraDirection = glm::vec4(0, 0, 1, 0);
	}

	// Move Camera
	if (!ImGui::IsAnyItemActive())
	{
		float frame_speed_scale = ImGui::GetIO().DeltaTime / (1.0f / 60.0f);
		float move_speed = gCameraSettings.mMoveRotateSpeed.x * frame_speed_scale;
		if (ImGui::GetIO().KeyShift)
			move_speed *= 20.0f;
		if (ImGui::GetIO().KeyCtrl)
			move_speed *= 0.1f;

		if (ImGui::IsKeyDown('W'))
			gPerFrameConstantBuffer.mCameraPosition += gPerFrameConstantBuffer.mCameraDirection * move_speed;
		if (ImGui::IsKeyDown('S'))
			gPerFrameConstantBuffer.mCameraPosition -= gPerFrameConstantBuffer.mCameraDirection * move_speed;

		glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(gPerFrameConstantBuffer.mCameraDirection), glm::vec3(0, 1, 0))), 0);

		if (ImGui::IsKeyDown('A'))
			gPerFrameConstantBuffer.mCameraPosition -= right * move_speed;
		if (ImGui::IsKeyDown('D'))
			gPerFrameConstantBuffer.mCameraPosition += right * move_speed;

		glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(right), glm::vec3(gPerFrameConstantBuffer.mCameraDirection))), 0);

		if (ImGui::IsKeyDown('Q'))
			gPerFrameConstantBuffer.mCameraPosition -= up * move_speed;
		if (ImGui::IsKeyDown('E'))
			gPerFrameConstantBuffer.mCameraPosition += up * move_speed;
	}

	// Frustum
	{
		float horizontal_tan = glm::tan(gCameraSettings.mHorizontalFovDegree * 0.5f * glm::pi<float>() / 180.0f);

		glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(gPerFrameConstantBuffer.mCameraDirection), glm::vec3(0, 1, 0))), 0);
		gPerFrameConstantBuffer.mCameraRightExtend = right * horizontal_tan;

		glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(right), glm::vec3(gPerFrameConstantBuffer.mCameraDirection))), 0);
		gPerFrameConstantBuffer.mCameraUpExtend = up * horizontal_tan * (gDisplaySettings.mRenderResolution.y * 1.0f / gDisplaySettings.mRenderResolution.x);
	}

	// ImGUI
	{
		ImGui::Begin("DXR Playground");
		{
			ImGui::Text("Time %.3f @ Average %.3f ms/frame (%.1f FPS) @ %dx%d",
				gPerFrameConstantBuffer.mTime,
				1000.0f / ImGui::GetIO().Framerate,
				ImGui::GetIO().Framerate,
				gDisplaySettings.mRenderResolution.x,
				gDisplaySettings.mRenderResolution.y);

			{
				if (ImGui::Button("Reset Camera Transform"))
				{
					gPerFrameConstantBuffer.mCameraPosition = kScenePresets[(int)sCurrentScene].mCameraPosition;
					gPerFrameConstantBuffer.mCameraDirection = glm::normalize(kScenePresets[(int)sCurrentScene].mCameraDirection);
					gCameraSettings.mHorizontalFovDegree = kScenePresets[(int)sCurrentScene].mHorizontalFovDegree;
				}

				ImGui::SameLine();

				if (ImGui::Button("Reload Shader") || ImGui::IsKeyPressed(VK_F5))
					sReloadRequested = true;

				if (ImGui::Button("Dump Output") || ImGui::IsKeyPressed(VK_F9))
				{
					gDumpTextureProxy.mResource = gScene.GetOutputResource();
					gDumpTextureProxy.mName = "Output";
					gDumpTexture = &gDumpTextureProxy;
				}

				ImGui::SameLine();

				ImGui::CheckboxFlags("Output Luminance", &gPerFrameConstantBuffer.mOutputLuminance, 0x1);

				ImGui::SameLine();

				ImGui::Checkbox("Inline Raytracing", &gUseDXRInlineShader);
			}

			// Always update
			gPerFrameConstantBuffer.mEV100 = glm::log2((gCameraSettings.mExposureControl.mAperture * gCameraSettings.mExposureControl.mAperture) / (1.0f / gCameraSettings.mExposureControl.mInvShutterSpeed) * 100.0f / gCameraSettings.mExposureControl.mSensitivity);
			if (ImGui::TreeNodeEx("Camera"))
			{
				auto s = [](float pivot = ImGui::GetCursorPosX()) { ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.65f - (ImGui::GetCursorPosX() - pivot)); };
	
				ImGui::InputFloat3("Position", (float*)&gPerFrameConstantBuffer.mCameraPosition);
				ImGui::InputFloat3("Direction", (float*)&gPerFrameConstantBuffer.mCameraDirection);
				ImGui::SliderFloat("Horz Fov", (float*)&gCameraSettings.mHorizontalFovDegree, 30.0f, 160.0f);

				ImGui::PushID("Aperture");
				{
					float x = ImGui::GetCursorPosX();

					if (ImGui::Button("<")) { gCameraSettings.mExposureControl.mAperture /= glm::sqrt(2.0f); }
					ImGui::SameLine();
					if (ImGui::Button(">")) { gCameraSettings.mExposureControl.mAperture *= glm::sqrt(2.0f); }
					ImGui::SameLine();
					s(x); ImGui::SliderFloat("Aperture (f/_)", &gCameraSettings.mExposureControl.mAperture, 1.0f, 22.0f);
				}
				ImGui::PopID();
				ImGui::PushID("Shutter Speed");
				{
					float x = ImGui::GetCursorPosX();

					if (ImGui::Button("<")) { gCameraSettings.mExposureControl.mInvShutterSpeed /= 2.0f; }
					ImGui::SameLine();
					if (ImGui::Button(">")) { gCameraSettings.mExposureControl.mInvShutterSpeed *= 2.0f; }
					ImGui::SameLine();
					s(x); ImGui::SliderFloat("Shutter Speed (1/_ sec)", &gCameraSettings.mExposureControl.mInvShutterSpeed, 1.0f, 500.0f);
				}
				ImGui::PopID();
				s(); ImGui::SliderFloat("ISO", &gCameraSettings.mExposureControl.mSensitivity, 100.0f, 1000.0f);

				if (ImGui::SmallButton("Reset Exposure"))
					gCameraSettings.ResetExposure();
				ImGui::SameLine();
				ImGui::Text("EV100 = %.2f", gPerFrameConstantBuffer.mEV100);

				ImGui::Text("Tonemap");
				for (int i = 0; i < (int)TonemapMode::Count; i++)
				{
					const auto& name = nameof::nameof_enum((TonemapMode)i);
					ImGui::SameLine();
					ImGui::RadioButton(name.data(), (int*)&gPerFrameConstantBuffer.mTonemapMode, i);
				}

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Render"))
			{
				ImGui::Text("Recursion Mode");
				for (int i = 0; i < (int)RecursionMode::Count; i++)
				{
					const auto& name = nameof::nameof_enum((RecursionMode)i);
					ImGui::SameLine();
					ImGui::RadioButton(name.data(), (int*)&gPerFrameConstantBuffer.mRecursionMode, i);
				}
				ImGui::SliderInt("Recursion Count Max", (int*)&gPerFrameConstantBuffer.mRecursionCountMax, 0, 8);

				for (int i = 0; i < (int)DebugMode::Count; i++)
				{
					const auto& name = nameof::nameof_enum((DebugMode)i);
					if (name[0] == '_')
					{
						ImGui::NewLine();
						continue;
					}

					if (i != 0)
						ImGui::SameLine();

					ImGui::RadioButton(name.data(), (int*)&gPerFrameConstantBuffer.mDebugMode, i);
				}

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Instance"))
			{
				for (int i = 0; i < (int)DebugInstanceMode::Count; i++)
				{
					const auto& name = nameof::nameof_enum((DebugInstanceMode)i);
					if (name[0] == '_')
					{
						ImGui::NewLine();
						continue;
					}

					if (i != 0)
						ImGui::SameLine();

					ImGui::RadioButton(name.data(), (int*)&gPerFrameConstantBuffer.mDebugInstanceMode, i);
				}

				ImGui::SliderInt("DebugInstanceIndex", (int*)&gPerFrameConstantBuffer.mDebugInstanceIndex, 0u, gScene.GetInstanceCount() - 1);
				gPerFrameConstantBuffer.mDebugInstanceIndex = glm::clamp(gPerFrameConstantBuffer.mDebugInstanceIndex, 0u, gScene.GetInstanceCount() - 1);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Scene"))
			{
				for (int i = 0; i < (int)ScenePresetType::COUNT; i++)
				{
					if (kScenePresets[i].mPath == nullptr || std::filesystem::exists(kScenePresets[i].mPath))
						ImGui::RadioButton(kScenePresets[i].mName, (int*)&sCurrentScene, i);
				}

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Atmosphere", ImGuiTreeNodeFlags_DefaultOpen))
			{
				gPrecomputedAtmosphereScattering.UpdateImGui();
				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Cloud"))
			{
				gCloud.UpdateImGui();
				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("DDGI"))
			{
				gDDGI.UpdateImGui();
				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Display"))
			{
				ImGui::Checkbox("Vsync", &gDisplaySettings.mVsync);

				auto resize = [](int width, int height)
				{
					RECT rect = { 0, 0, width, height };
					AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
					::SetWindowPos(::GetActiveWindow(), NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
				};

				if (ImGui::Button("640 x 480"))
					resize(640, 480);

				if (ImGui::Button("800 x 600"))
					resize(800, 600);

				if (ImGui::Button("1280 x 720"))
					resize(1280, 720);

				if (ImGui::Button("1280 x 800"))
					resize(1280, 800);

				if (ImGui::Button("1920 x 1080"))
					resize(1920, 1080);

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("Log", ImGuiTreeNodeFlags_DefaultOpen))
			{
				gLog.Draw();

				ImGui::TreePop();
			}
		}
		ImGui::End();
	}

	{
		// Reload
		if (sReloadRequested)
		{
			sReloadRequested = false;

			sWaitForLastSubmittedFrame();
			gScene.RebuildShader();
			gPerFrameConstantBuffer.mReset = 1;

			gPrecomputedAtmosphereScattering.mRecomputeRequested = true;
			gCloud.mRecomputeRequested = true;
			gDDGI.mRecomputeRequested = true;
		}
	}
}

// Main code
int WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, PSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
	// Create application window
	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, sWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, kApplicationTitleW, nullptr };
	::RegisterClassEx(&wc);

	RECT rect = { 0, 0, 1280, 720 };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
	HWND hwnd = ::CreateWindow(wc.lpszClassName, kApplicationTitleW, WS_OVERLAPPEDWINDOW, 100, 100, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, nullptr);

	// Initialize Direct3D
	if (!sCreateDeviceD3D(hwnd))
	{
		sCleanupDeviceD3D();
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}
	sCreateRenderTarget();

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
	ImGui_ImplDX12_Init(gDevice, NUM_FRAMES_IN_FLIGHT, DXGI_FORMAT_R8G8B8A8_UNORM);
	ImGui_ImplDX12_CreateDeviceObjects();

	// Create Scene
	gScene.Load(kScenePresets[(int)sCurrentScene].mPath, kScenePresets[(int)sCurrentScene].mTransform);
	gScene.Build(gCommandList);
	gPerFrameConstantBuffer.mCameraPosition = kScenePresets[(int)sCurrentScene].mCameraPosition;
	gPerFrameConstantBuffer.mCameraDirection = glm::normalize(kScenePresets[(int)sCurrentScene].mCameraDirection);
	gCameraSettings.mHorizontalFovDegree = kScenePresets[(int)sCurrentScene].mHorizontalFovDegree;
	gPerFrameConstantBuffer.mSunAzimuth = kScenePresets[(int)sCurrentScene].mSunAzimuth;
	gPerFrameConstantBuffer.mSunZenith = kScenePresets[(int)sCurrentScene].mSunZenith;

	// Features (rely on ImGui, Scene)
	gPrecomputedAtmosphereScattering.Initialize();
	gCloud.Initialize();
	gDDGI.Initialize();

	// File watch
	filewatch::FileWatch<std::string> file_watch("Shader/", 
		[] (const std::string& inPath, const filewatch::Event /*inChangeType*/) 
		{
			std::regex pattern(".*\\.hlsl");
			if (std::regex_match(inPath, pattern) && inPath.find("Generated") == std::string::npos)
			{
				std::string msg = "Reload triggered by " + inPath + "\n";
				gLog.AddLog(msg.c_str());

				sReloadRequested = true;
			}
		});

	gCommandList->Close();
	gCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&gCommandList);
	uint64_t one_shot_fence_value = 0xff;
	gCommandQueue->Signal(gIncrementalFence, one_shot_fence_value); // abuse fence to wait only during initialization
	gIncrementalFence->SetEventOnCompletion(one_shot_fence_value, gIncrementalFenceEvent);
	WaitForSingleObject(gIncrementalFenceEvent, INFINITE);

	// Show the window
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

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

		gPrecomputedAtmosphereScattering.Finalize();
		gCloud.Finalize();
		gDDGI.Finalize();

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		// Customization - Cleanup
		gScene.Unload();

		sCleanupRenderTarget();
		sCleanupDeviceD3D();
		::DestroyWindow(hwnd);
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
	}

	return 0;
}

void sRender()
{
	// Frame Context
	FrameContext* frameCtxt = sWaitForNextFrameResources();
	glm::uint32 frame_index = gSwapChain->GetCurrentBackBufferIndex();
	ID3D12Resource* frame_render_target_resource = gBackBufferRenderTargetResource[frame_index];
	D3D12_CPU_DESCRIPTOR_HANDLE& frame_render_target_descriptor_handle = gBackBufferRenderTargetRTV[frame_index];

	// Frame Begin
	{
		frameCtxt->mCommandAllocator->Reset();
		gCommandList->Reset(frameCtxt->mCommandAllocator, nullptr);
		
		gBarrierTransition(gCommandList, frame_render_target_resource, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	}

	// Update and Upload
	{
		if (sPreviousScene != sCurrentScene)
		{
			sPreviousScene = sCurrentScene;

			sWaitForIdle();

			gScene.Unload();
			gScene.Load(kScenePresets[(int)sCurrentScene].mPath, kScenePresets[(int)sCurrentScene].mTransform);
			gScene.Build(gCommandList);

			gPerFrameConstantBuffer.mCameraPosition = kScenePresets[(int)sCurrentScene].mCameraPosition;
			gPerFrameConstantBuffer.mCameraDirection = glm::normalize(kScenePresets[(int)sCurrentScene].mCameraDirection);
			gCameraSettings.mHorizontalFovDegree = kScenePresets[(int)sCurrentScene].mHorizontalFovDegree;
			gPerFrameConstantBuffer.mSunAzimuth = kScenePresets[(int)sCurrentScene].mSunAzimuth;
			gPerFrameConstantBuffer.mSunZenith = kScenePresets[(int)sCurrentScene].mSunZenith;
		}
		else
			gScene.Update(gCommandList);
	}

	// Upload - PerFrame
	{
		{
			gPerFrameConstantBuffer.mSunDirection = 
				glm::vec4(0,1,0,0) * glm::rotate(gPerFrameConstantBuffer.mSunZenith, glm::vec3(0, 0, 1)) * glm::rotate(gPerFrameConstantBuffer.mSunAzimuth + glm::pi<float>() / 2.0f, glm::vec3(0, 1, 0));
		}

		// Accumulation reset check
		{
			static ShaderType::PerFrame sPerFrameCopy = gPerFrameConstantBuffer;

			sPerFrameCopy.mDebugCoord = gPerFrameConstantBuffer.mDebugCoord = glm::uvec2((glm::uint32)ImGui::GetMousePos().x, (glm::uint32)ImGui::GetMousePos().y);
			sPerFrameCopy.mAccumulationFrameCount = gPerFrameConstantBuffer.mAccumulationFrameCount;
			sPerFrameCopy.mFrameIndex = gPerFrameConstantBuffer.mFrameIndex;
			sPerFrameCopy.mTime = gPerFrameConstantBuffer.mTime;

			if (gPerFrameConstantBuffer.mReset == 0 && memcmp(&sPerFrameCopy, &gPerFrameConstantBuffer, sizeof(ShaderType::PerFrame)) == 0)
				gPerFrameConstantBuffer.mAccumulationFrameCount++;
			else
				gPerFrameConstantBuffer.mAccumulationFrameCount = 1;

			gPerFrameConstantBuffer.mReset = 0;
			sPerFrameCopy = gPerFrameConstantBuffer;
		}
		
		memcpy(frameCtxt->mConstantUploadBufferPointer, &gPerFrameConstantBuffer, sizeof(gPerFrameConstantBuffer));

		gBarrierTransition(gCommandList, gConstantGPUBuffer.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
		gCommandList->CopyResource(gConstantGPUBuffer.Get(), frameCtxt->mConstantUploadBuffer);
		gBarrierTransition(gCommandList, gConstantGPUBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	}

	// Atmosphere
	{
		gPrecomputedAtmosphereScattering.Update();
		gPrecomputedAtmosphereScattering.Load();
		gPrecomputedAtmosphereScattering.Precompute();
		gPrecomputedAtmosphereScattering.Compute();
	}

	// Cloud
	{
		gCloud.Update();
		gCloud.Precompute();
	}

	// DDGI
	{
		gDDGI.Update();
		gDDGI.Precompute();
	}

	// Raytrace
	{
		gRaytrace();
	}

	// Copy
	{
		// Output: UAV -> Copy
		gBarrierTransition(gCommandList, gScene.GetOutputResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

		// Draw
		D3D12_RESOURCE_DESC desc = frame_render_target_resource->GetDesc();
		D3D12_VIEWPORT viewport = {};
		viewport.Width = (float)desc.Width;
		viewport.Height = (float)desc.Height;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		gCommandList->RSSetViewports(1, &viewport);
		D3D12_RECT rect = {};
		rect.left = 0;
		rect.top = 0;
		rect.right = (LONG)desc.Width;
		rect.bottom = (LONG)desc.Height;
		gCommandList->RSSetScissorRects(1, &rect);
		gCommandList->OMSetRenderTargets(1, &frame_render_target_descriptor_handle, false, nullptr);
		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		gCompositeShader.SetupGraphics();
		gCommandList->DrawInstanced(3, 1, 0, 0);

		// Output - Copy -> UAV
		gBarrierTransition(gCommandList, gScene.GetOutputResource(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}

	// Draw ImGui
	{
		ImGui::Render();

		gCommandList->OMSetRenderTargets(1, &frame_render_target_descriptor_handle, false, nullptr);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);
	}

	// Frame End
	{
		gBarrierTransition(gCommandList, frame_render_target_resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		gCommandList->Close();
		gCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&gCommandList);

		gPerFrameConstantBuffer.mTime += ImGui::GetIO().DeltaTime;
		gPerFrameConstantBuffer.mFrameIndex++;
	}

	// Dump Texture
	{
		if (gDumpTexture != nullptr && gDumpTexture->mResource != nullptr)
		{
			D3D12_RESOURCE_STATES resource_state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
			if (gScene.GetOutputResource() == gDumpTexture->mResource.Get())
				resource_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

			DirectX::ScratchImage image;
			DirectX::CaptureTexture(gCommandQueue, gDumpTexture->mResource.Get(), false, image, resource_state, resource_state);

			std::wstring directory = L"TextureDump/";
			std::filesystem::create_directory(directory);
			std::wstring path = directory + gToWString(gDumpTexture->mName) + L".dds";
			DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, path.c_str());
			
			gDumpTexture = nullptr;
		}
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

			// GBV don't work well yet
//  		if (SUCCEEDED(dx12Debug->QueryInterface(IID_PPV_ARGS(&dx12Debug1))))
//  			dx12Debug1->SetEnableGPUBasedValidation(true);
		}
	}

	// Create device with highest feature level as possible
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_12_2;
	if (D3D12CreateDevice(nullptr, feature_level, IID_PPV_ARGS(&gDevice)) != S_OK)
		return false;

	// Check shader model
	D3D12_FEATURE_DATA_SHADER_MODEL shader_model = { D3D_SHADER_MODEL_6_6 };
	if (FAILED(gDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
		|| (shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6))
		return false;

	// Check DXR
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	if (FAILED(gDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5)))
		|| options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
		return false;

	// RTV Descriptor Heap
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = NUM_BACK_BUFFERS;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;
		if (gDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gRTVDescriptorHeap)) != S_OK)
			return false;

		gRTVDescriptorHeap->SetName(L"RTVDescriptorHeap");

		SIZE_T rtvDescriptorSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = gRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			gBackBufferRenderTargetRTV[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	// CommandQueue
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

		// Buffer
		{
			D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(ShaderType::PerFrame), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gFrameContext[i].mConstantUploadBuffer)));
			name = L"Constant_Upload_" + i;
			gFrameContext[i].mConstantUploadBuffer->SetName(name.c_str());

			// Persistent map - https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map#advanced-usage-models
			gFrameContext[i].mConstantUploadBuffer->Map(0, nullptr, (void**)&gFrameContext[i].mConstantUploadBufferPointer);
		}
	}

	// PerFrame (GPU)
	{
		// Buffer
		{
			D3D12_RESOURCE_DESC resource_desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(ShaderType::PerFrame), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr, IID_PPV_ARGS(&gConstantGPUBuffer)));
			std::wstring name = L"Constant_GPU";
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

	gConstantGPUBuffer = nullptr;
	gSafeRelease(gCommandQueue);
	gSafeRelease(gCommandList);
	gSafeRelease(gRTVDescriptorHeap);
	gSafeRelease(gIncrementalFence);
	gSafeCloseHandle(gIncrementalFenceEvent);
	gSafeRelease(gDevice);

	if (DX12_ENABLE_DEBUG_LAYER)
	{
		ComPtr<IDXGIDebug1> dxgi_debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
			dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
	}
}

static void sCreateRenderTarget()
{
	ID3D12Resource* pBackBuffer;
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		gSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		gDevice->CreateRenderTargetView(pBackBuffer, nullptr, gBackBufferRenderTargetRTV[i]);
		gBackBufferRenderTargetResource[i] = pBackBuffer;
	}
}

static void sCleanupRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		gSafeRelease(gBackBufferRenderTargetResource[i]);
}

static void sWaitForIdle()
{
	gIncrementalFence->SetEventOnCompletion(gFenceLastSignaledValue, gIncrementalFenceEvent);
	WaitForSingleObject(gIncrementalFenceEvent, INFINITE);
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

			gScene.RebuildBinding([&]()
			{
				sCleanupRenderTarget();
				gDisplaySettings.mRenderResolution.x = gMax((UINT)LOWORD(lParam), 8u);
				gDisplaySettings.mRenderResolution.y = gMax((UINT)HIWORD(lParam), 8u);
				DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
				gSwapChain->GetDesc1(&swap_chain_desc);
				gSwapChain->ResizeBuffers(
					swap_chain_desc.BufferCount,
					gDisplaySettings.mRenderResolution.x,
					gDisplaySettings.mRenderResolution.y,
					swap_chain_desc.Format,
					swap_chain_desc.Flags);
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
