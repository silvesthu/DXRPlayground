#include "Common.h"

#include "Renderer.h"
#include "Color.h"
#include "Scene.h"

#include "Atmosphere.h"
#include "Cloud.h"

#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"

#pragma warning(push)
#pragma warning(disable: 4068)
#include "Thirdparty/filewatch/FileWatch.hpp"
#pragma warning(pop)

#define DX12_ENABLE_DEBUG_LAYER			(0)
#define DX12_ENABLE_GBV					(0)
#define DX12_ENABLE_INFO_QUEUE_CALLBACK (0)
#define DX12_ENABLE_PIX_CAPTURE			(0)

static const wchar_t*					kApplicationTitleW = L"DXR Playground";

struct ScenePreset
{
#define SCENE_PRESET_MEMBER(type, name, default_value) MEMBER(ScenePreset, type, name, default_value)

	SCENE_PRESET_MEMBER(const char*, 	Name, 					nullptr);
	SCENE_PRESET_MEMBER(const char*, 	Path, 					nullptr);
	SCENE_PRESET_MEMBER(glm::vec4, 		CameraPosition, 		glm::vec4(0, 1, 0, 1));
	SCENE_PRESET_MEMBER(glm::vec4, 		CameraDirection, 		glm::vec4(0, 0, -1, 0));
	SCENE_PRESET_MEMBER(float, 			HorizontalFovDegree, 	90);
	SCENE_PRESET_MEMBER(glm::mat4x4, 	Transform, 				glm::mat4x4(1));
	SCENE_PRESET_MEMBER(float, 			SunAzimuth, 			0);
	SCENE_PRESET_MEMBER(float, 			SunZenith, 				glm::pi<float>() / 4.0f);
	SCENE_PRESET_MEMBER(AtmosphereMode, Atmosphere,				AtmosphereMode::ConstantColor);
};

enum class ScenePresetType
{
	None,

	CornellBox,
	CornellBoxTeapot,
	CornellBoxMonkey,
	VeachMIS,
	LivingRoom2,

	Bruneton17,
	Bruneton17_Artifact_Mu,
	Hillaire20,

	IES,

	Count,
};

static ScenePresetType sCurrentScene = ScenePresetType::VeachMIS;
static ScenePresetType sPreviousScene = sCurrentScene;
static ScenePreset kScenePresets[(int)ScenePresetType::Count] =
{
	ScenePreset().Name("None"),
	ScenePreset().Name("CornellBox").Path("Asset/Comparison/benedikt-bitterli/cornell-box/scene_v3.xml"),
	ScenePreset().Name("CornellBoxTeapot").Path("Asset/Comparison/benedikt-bitterli/cornell-box-teapot/scene_v3.xml"),
	ScenePreset().Name("CornellMonkey").Path("Asset/Comparison/benedikt-bitterli/cornell-box-monkey/scene_v3.xml"),
	ScenePreset().Name("VeachMIS").Path("Asset/Comparison/benedikt-bitterli/veach-mis/scene_ggx_v3.xml"),
	ScenePreset().Name("LivingRoom2").Path("Asset/Comparison/benedikt-bitterli/living-room-2/scene_v3.xml"),
	
	ScenePreset().Name("Bruneton17").Path("Asset/primitives/sphere.obj").CameraPosition(glm::vec4(0.0f, 0.0f, 9.0f, 0.0f)).CameraDirection(glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)).Transform(glm::translate(glm::vec3(0.0f, 1.0f, 0.0f))).Atmosphere(AtmosphereMode::Bruneton17),
	ScenePreset().Name("Bruneton17_Artifact_Mu").Path("Asset/primitives/sphere.obj").CameraPosition(glm::vec4(0.0f, 80.0f, 150.0f, 0.0f)).CameraDirection(glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)).Transform(glm::scale(glm::vec3(100.0f, 100.0f, 100.0f))).Atmosphere(AtmosphereMode::Bruneton17),
	ScenePreset().Name("Hillaire20").CameraPosition(glm::vec4(0.0f, 0.5, -1.0f, 0.0f)).CameraDirection(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)).HorizontalFovDegree(98.8514328f).Transform(glm::translate(glm::vec3(0.0f, 1.0f, 0.0f))).SunZenith(glm::pi<float>() / 2.0f - 0.45f).Atmosphere(AtmosphereMode::Hillaire20),

	ScenePreset().Name("IES").Path("Asset/IES/007cfb11e343e2f42e3b476be4ab684e/scene_v3.xml"),
};

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
static void sWaitForGPU();
static void sWaitForFrameContext();
static void sUpdate();
static void sLoadShader() { gRenderer.mReloadShader = true; }
static void sLoadCamera();
static void sLoadScene();
static void sDumpLuminance()
{
	gDumpTextureProxy.mResource = gRenderer.mRuntime.mScreenColorTexture.mResource;
	gDumpTextureProxy.mName = "Luminance";
	gDumpTexture = &gDumpTextureProxy;
}
static void sRender();
static LRESULT WINAPI sWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void sPrepareImGui()
{
	std::string stat = std::format("Frame {} | Time {:.3f} s | Average {:.3f} ms (FPS {:.1f}) | {}x{}###DXRPlayground",
		gConstants.mCurrentFrameIndex,
		gConstants.mTime,
		1000.0f / ImGui::GetIO().Framerate,
		ImGui::GetIO().Framerate,
		gDisplaySettings.mRenderResolution.x,
		gDisplaySettings.mRenderResolution.y);
	if (ImGui::Begin(stat.c_str()))
	{
		{
			if (ImGui::Button("Reload Shader (F5)"))
				sLoadShader();

			ImGui::SameLine();

			if (ImGui::Button("Reload Camera (F6)"))
				sLoadCamera();

			ImGui::SameLine();

			if (ImGui::Button("Copy Camera"))
			{
				// Matrix for Mitsuba3
				glm::mat4x4 camera_transform = glm::mat4x4(1.0f);
				camera_transform[3] = gConstants.mCameraPosition;
				camera_transform[2] = gConstants.mCameraDirection;
				camera_transform[1] = glm::normalize(gConstants.mCameraUpExtend);
				camera_transform[0] = -glm::normalize(gConstants.mCameraRightExtend);
				ImGui::SetClipboardText(gToString(camera_transform).c_str());
			}
		}
		{
			if (ImGui::Button("Dump Luminance (F9)"))
				sDumpLuminance();

			ImGui::SameLine();

			if (ImGui::Button("Open Dump Folder"))
				gOpenDumpFolder();

			ImGui::SameLine();

			if (ImGui::Button("Open Scene Folder"))
				gOpenSceneFolder(kScenePresets[(int)sCurrentScene].mPath);
		}

		if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputInt2("Coords", (int*)&gConstants.mPixelDebugCoord);
			ImGui::InputFloat3("Pixel Value", &gGetFrameContext().mDebugReadbackBufferPointer->mPixelValue.x, "%.8f", ImGuiInputTextFlags_ReadOnly);
			if (ImGui::TreeNodeEx("Pixel Debug Value (on each hit)"))
			{
				for (int i = 0; i < static_cast<int>(PixelDebugMode::Count); i++)
				{
					const auto& name = nameof::nameof_enum(static_cast<PixelDebugMode>(i));
					if (name[0] == '_')
					{
						ImGui::NewLine();
						continue;
					}

					if (i != 0)
						ImGui::SameLine();

					ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mPixelDebugMode), i);
				}

				for (int i = 0; i < Debug::kValueArraySize; i++)
					ImGui::InputFloat4(std::to_string(i).c_str(), &gGetFrameContext().mDebugReadbackBufferPointer->mPixelValueArray[i].x, "%.8f", ImGuiInputTextFlags_ReadOnly);

				ImGui::TreePop();
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Sampling", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Offset Mode");
			for (int i = 0; i < static_cast<int>(OffsetMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<OffsetMode>(i));
				ImGui::SameLine();
				ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mOffsetMode), i);
			}

			ImGui::Text("Sample Mode");
			for (int i = 0; i < static_cast<int>(SampleMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<SampleMode>(i));
				ImGui::SameLine();
				ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mSampleMode), i);
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Accumulation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Checkbox("Frame Count Unlimited", &gRenderer.mAccumulationFrameUnlimited))
				gRenderer.mAccumulationResetRequested = true;
			ImGui::SameLine();
			ImGui::Checkbox("Paused", &gRenderer.mAccumulationPaused);

			if (!gRenderer.mAccumulationFrameUnlimited)
				if (ImGui::SliderInt("Frame Count", reinterpret_cast<int*>(&gRenderer.mAccumulationFrameCount), 1, 64))
					gRenderer.mAccumulationResetRequested = true;

			ImGui::Text("Recursion Mode");
			for (int i = 0; i < static_cast<int>(RecursionMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<RecursionMode>(i));
				ImGui::SameLine();
				ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mRecursionMode), i);
			}
			ImGui::SliderInt("Recursion Count Max", reinterpret_cast<int*>(&gConstants.mRecursionCountMax), 0, 16);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Camera"))
		{
			auto align_right = [](float pivot = ImGui::GetCursorPosX()) { ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.65f - (ImGui::GetCursorPosX() - pivot)); };

			ImGui::InputFloat3("Position", (float*)&gConstants.mCameraPosition);
			ImGui::InputFloat3("Direction", (float*)&gConstants.mCameraDirection);
			ImGui::SliderFloat("Horz Fov", (float*)&gCameraSettings.mHorizontalFovDegree, 30.0f, 160.0f);

			ImGui::PushID("Aperture");
			{
				float x = ImGui::GetCursorPosX();

				if (ImGui::Button("<")) { gCameraSettings.mExposureControl.mAperture /= glm::sqrt(2.0f); }
				ImGui::SameLine();
				if (ImGui::Button(">")) { gCameraSettings.mExposureControl.mAperture *= glm::sqrt(2.0f); }
				ImGui::SameLine();
				align_right(x); ImGui::SliderFloat("Aperture", &gCameraSettings.mExposureControl.mAperture, 1.0f, 22.0f);
			}
			ImGui::PopID();
			ImGui::PushID("Shutter Speed");
			{
				float x = ImGui::GetCursorPosX();

				if (ImGui::Button("<")) { gCameraSettings.mExposureControl.mInvShutterSpeed /= 2.0f; }
				ImGui::SameLine();
				if (ImGui::Button(">")) { gCameraSettings.mExposureControl.mInvShutterSpeed *= 2.0f; }
				ImGui::SameLine();
				align_right(x); ImGui::SliderFloat("Shutter Speed (1/sec)", &gCameraSettings.mExposureControl.mInvShutterSpeed, 1.0f, 500.0f);
			}
			ImGui::PopID();
			align_right(); ImGui::SliderFloat("ISO", &gCameraSettings.mExposureControl.mSensitivity, 100.0f, 1000.0f);

			if (ImGui::SmallButton("Reset Exposure"))
				gCameraSettings.ResetExposure();
			ImGui::SameLine();
			ImGui::Text("EV100 = %.2f", gConstants.mEV100);

			ImGui::Text("ToneMappingMode");
			for (int i = 0; i < static_cast<int>(ToneMappingMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<ToneMappingMode>(i));
				ImGui::SameLine();
				ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mToneMappingMode), i);
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Render"))
		{
			for (int i = 0; i < static_cast<int>(DebugMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<DebugMode>(i));
				if (name[0] == '_')
				{
					ImGui::NewLine();
					continue;
				}

				if (i != 0)
					ImGui::SameLine();

				ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mDebugMode), i);
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Scene"))
		{
			for (int i = 0; i < static_cast<int>(ScenePresetType::Count); i++)
			{
				if (kScenePresets[i].mPath == nullptr || std::filesystem::exists(kScenePresets[i].mPath))
					ImGui::RadioButton(kScenePresets[i].mName, reinterpret_cast<int*>(&sCurrentScene), i);
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Atmosphere"))
		{
			gAtmosphere.ImGuiShowMenus();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Cloud"))
		{
			gCloud.ImGuiShowMenus();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Shader"))
		{
			if (ImGui::Button("Print Disassembly RayQuery"))
			{
				gRenderer.mDumpDisassemblyRayQuery = true;
				gRenderer.mReloadShader = true;
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Display"))
		{
			ImGui::Checkbox("Vsync", &gDisplaySettings.mVsync);

			if (ImGui::Button("640 x 480"))
				gRenderer.Resize(640, 480);

			if (ImGui::Button("800 x 600"))
				gRenderer.Resize(800, 600);

			if (ImGui::Button("1280 x 720"))
				gRenderer.Resize(1280, 720);

			if (ImGui::Button("1280 x 800"))
				gRenderer.Resize(1280, 800);

			if (ImGui::Button("1920 x 1080"))
				gRenderer.Resize(1920, 1080);

			ImGui::TreePop();
		}

		// Floating items
		{
			gRenderer.ImGuiShowTextures();
			gScene.ImGuiShowTextures();
			gAtmosphere.ImGuiShowTextures();
			gCloud.ImGuiShowTextures();

			if (ImGui::Begin("Instances"))
			{
				for (int i = 0; i < static_cast<int>(DebugInstanceMode::Count); i++)
				{
					const auto& name = nameof::nameof_enum(static_cast<DebugInstanceMode>(i));
					if (name[0] == '_')
					{
						ImGui::NewLine();
						continue;
					}

					if (i != 0)
						ImGui::SameLine();

					ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mDebugInstanceMode), i);
				}

				const char* columns[] =
				{
					"Index",
					"Name",
					"Position",
					"BSDFType",
					"Albedo",
					"Reflectance",
					"Transmittance",
					"Emission",
					"RoughnessAlpha",
					"Opacity",
				};
				int column_count = (int)std::size(columns);

				if (ImGui::BeginTable("Table", column_count, ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
				{
					ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible

					ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
					for (int i = 0; i < column_count; i++)
					{
						ImGui::TableSetColumnIndex(i);
						ImGui::Text(columns[i]);
					}

					for (int row = 0; row < gScene.GetInstanceCount(); row++)
					{
						ImGui::TableNextRow();

						const InstanceInfo& instance_info = gScene.GetInstanceInfo(row);
						const InstanceData& instance_data = gScene.GetInstanceData(row);

						int column_index = 0;

						ImGui::TableSetColumnIndex(column_index++);
						if (ImGui::Selectable(std::to_string(row).c_str(), row == gConstants.mDebugInstanceIndex, ImGuiSelectableFlags_SpanAllColumns))
							gConstants.mDebugInstanceIndex = row;

						if (row == gGetFrameContext().mDebugReadbackBufferPointer->mPixelInstanceID)
							ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.8f, 0.2f, 0.2f, 0.8f)));

						ImGui::TableSetColumnIndex(column_index++);
						ImGui::Text("%s", instance_info.mName.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string position = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mTransform[3][0], instance_data.mTransform[3][1], instance_data.mTransform[3][2]);
						position = glm::dot(glm::vec3(instance_data.mTransform[3]), glm::vec3(instance_data.mTransform[3])) != 0.0f ? position : "";
						ImGui::Text(position.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						ImGui::Text("%s%s", NAMEOF_ENUM(instance_data.mBSDFType).data(), instance_data.mTwoSided ? " (TwoSided)" : "");

						ImGui::TableSetColumnIndex(column_index++);
						std::string albedo = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mAlbedo.x, instance_data.mAlbedo.y, instance_data.mAlbedo.z);
						albedo = instance_data.mAlbedo != InstanceData().mAlbedo ? albedo : "";
						albedo = instance_info.mAlbedoTexture.empty() ? albedo : (instance_info.mAlbedoTexture.filename().string() + " (" + std::to_string(instance_data.mAlbedoTextureIndex) + ")");
						ImGui::Text(albedo.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string reflectance = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mSpecularReflectance.x, instance_data.mSpecularReflectance.y, instance_data.mSpecularReflectance.z);
						reflectance = instance_data.mSpecularReflectance != InstanceData().mSpecularReflectance ? reflectance : "";
						ImGui::Text(reflectance.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string transmittance = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mSpecularTransmittance.x, instance_data.mSpecularTransmittance.y, instance_data.mSpecularTransmittance.z);
						transmittance = instance_data.mSpecularTransmittance != InstanceData().mSpecularTransmittance ? transmittance : "";
						ImGui::Text(transmittance.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string emission = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mEmission.x, instance_data.mEmission.y, instance_data.mEmission.z);
						emission = instance_data.mEmission != InstanceData().mEmission ? emission : "";
						ImGui::Text(emission.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string roughness_alpha = std::format("{:.2f}", instance_data.mRoughnessAlpha);
						roughness_alpha = instance_data.mRoughnessAlpha != InstanceData().mRoughnessAlpha ? roughness_alpha : "";
						ImGui::Text(roughness_alpha.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string opacity = std::format("{:.2f}", instance_data.mOpacity);
						ImGui::Text(opacity.c_str());

						gAssert(column_index == column_count);
					}
					ImGui::EndTable();
				}

				gConstants.mDebugInstanceIndex = glm::clamp(gConstants.mDebugInstanceIndex, -1, gScene.GetInstanceCount() - 1);
			}
			ImGui::End();

			if (ImGui::Begin("Lights"))
			{
				static const int kColumnCount = 6;
				if (ImGui::BeginTable("Table", kColumnCount, ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
				{
					ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible

					{
						int column_index = 0;
						ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
						ImGui::TableSetColumnIndex(column_index++); ImGui::Text("Index");
						ImGui::TableSetColumnIndex(column_index++); ImGui::Text("Name");
						ImGui::TableSetColumnIndex(column_index++); ImGui::Text("Position");
						ImGui::TableSetColumnIndex(column_index++); ImGui::Text("HalfExtends");
						ImGui::TableSetColumnIndex(column_index++); ImGui::Text("Type");
						ImGui::TableSetColumnIndex(column_index++); ImGui::Text("Emission");
						gAssert(column_index == kColumnCount);
					}

					for (int row = 0; row < gScene.GetLightCount(); row++)
					{
						ImGui::TableNextRow();

						const Light& light = gScene.GetLight(row);
						const InstanceInfo& instance_info = gScene.GetInstanceInfo(light.mInstanceID);

						int column_index = 0;

						ImGui::TableSetColumnIndex(column_index++);
						if (ImGui::Selectable(std::to_string(row).c_str(), row == gConstants.mDebugLightIndex, ImGuiSelectableFlags_SpanAllColumns))
							gConstants.mDebugLightIndex = row;

						if ((int)light.mInstanceID == gGetFrameContext().mDebugReadbackBufferPointer->mPixelInstanceID)
							ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.8f, 0.2f, 0.2f, 0.8f)));

						ImGui::TableSetColumnIndex(column_index++);
						ImGui::Text("%s", instance_info.mName.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string position = std::format("{:.2f} {:.2f} {:.2f}", light.mPosition.x, light.mPosition.y, light.mPosition.z);
						ImGui::Text(position.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string half_extends = std::format("{:.2f} {:.2f}", light.mHalfExtends.x, light.mHalfExtends.y);
						ImGui::Text(half_extends.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						ImGui::Text("%s", NAMEOF_ENUM(light.mType).data());

						ImGui::TableSetColumnIndex(column_index++);
						std::string emission = std::format("{:.2f} {:.2f} {:.2f}", light.mEmission.x, light.mEmission.y, light.mEmission.z);
						emission = glm::dot(light.mEmission, light.mEmission) != 0.0f ? emission : "";
						ImGui::Text(emission.c_str());

						gAssert(column_index == kColumnCount);
					}
					ImGui::EndTable();
				}
			}
			ImGui::End();
		}
	}
	ImGui::End();
}

static void sUpdate()
{
	// Resize
	if (gRenderer.mResizeWidth != 0)
	{
		RECT rect = { 0, 0, gRenderer.mResizeWidth, gRenderer.mResizeHeight };
		AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
		::SetWindowPos(::GetActiveWindow(), NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);

		gRenderer.mResizeWidth = 0;
		gRenderer.mResizeHeight = 0;

	}

	// Reload
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
			glm::vec4 front = gConstants.mCameraDirection;
			glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(front), glm::vec3(0, 1, 0))), 0);
			glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(right), glm::vec3(front))), 0);

			front = glm::rotate(-mouse_delta.x * gCameraSettings.mMoveRotateSpeed.y, glm::vec3(up)) * front;
			front = glm::rotate(-mouse_delta.y * gCameraSettings.mMoveRotateSpeed.y, glm::vec3(right)) * front;

			gConstants.mCameraDirection = glm::normalize(front);
		}

		if (glm::isnan(gConstants.mCameraDirection.x))
			gConstants.mCameraDirection = glm::vec4(0, 0, 1, 0);
	}

	// Move Camera
	{
		float frame_speed_scale = ImGui::GetIO().DeltaTime / (1.0f / 60.0f);
		float move_speed = gCameraSettings.mMoveRotateSpeed.x * frame_speed_scale;
		if (ImGui::GetIO().KeyShift)
			move_speed *= 20.0f;
		if (ImGui::GetIO().KeyCtrl)
			move_speed *= 0.1f;

		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_W))
			gConstants.mCameraPosition += gConstants.mCameraDirection * move_speed;
		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_S))
			gConstants.mCameraPosition -= gConstants.mCameraDirection * move_speed;

		glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(gConstants.mCameraDirection), glm::vec3(0, 1, 0))), 0);

		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_A))
			gConstants.mCameraPosition -= right * move_speed;
		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_D))
			gConstants.mCameraPosition += right * move_speed;

		glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(right), glm::vec3(gConstants.mCameraDirection))), 0);

		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_Q))
			gConstants.mCameraPosition -= up * move_speed;
		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_E))
			gConstants.mCameraPosition += up * move_speed;

		if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_F5))
			sLoadShader();

		if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_F6))
			sLoadCamera();

		if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_F9))
			sDumpLuminance();

		if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_F10))
			gOpenDumpFolder();

		if (!ImGui::IsAnyItemFocused() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_UpArrow))
			gConstants.mPixelDebugCoord -= int2(0, 1);
		if (!ImGui::IsAnyItemFocused() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_DownArrow))
			gConstants.mPixelDebugCoord += int2(0, 1);
		if (!ImGui::IsAnyItemFocused() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_LeftArrow))
			gConstants.mPixelDebugCoord -= int2(1, 0);
		if (!ImGui::IsAnyItemFocused() && ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_RightArrow))
			gConstants.mPixelDebugCoord += int2(1, 0);
	}

	// Frustum
	{
		// Right-handed Y-up

		float horizontal_tan = glm::tan(gCameraSettings.mHorizontalFovDegree * 0.5f * glm::pi<float>() / 180.0f);

		glm::vec4 right = glm::vec4(glm::normalize(glm::cross(glm::vec3(gConstants.mCameraDirection), glm::vec3(0, 1, 0))), 0);
		gConstants.mCameraRightExtend = right * horizontal_tan;

		glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(right), glm::vec3(gConstants.mCameraDirection))), 0);
		gConstants.mCameraUpExtend = up * horizontal_tan * (gDisplaySettings.mRenderResolution.y * 1.0f / gDisplaySettings.mRenderResolution.x);
	}

	gAtmosphere.Update();
	gCloud.Update();
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

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	ImGui_ImplDX12_CreateShaderResourceViewCallback = [](ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
	{
		for (glm::uint i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		{
			gDevice->CreateShaderResourceView(resource, &desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(ViewDescriptorIndex::ImGuiFont));

			D3D12_SHADER_RESOURCE_VIEW_DESC null_desc = {};
			null_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			gDevice->CreateShaderResourceView(nullptr, &null_desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(ViewDescriptorIndex::ImGuiNull2D));
			null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			gDevice->CreateShaderResourceView(nullptr, &null_desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(ViewDescriptorIndex::ImGuiNull3D));
		}
	};

	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(gDevice, NUM_FRAMES_IN_FLIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, {}, {});
	ImGui_ImplDX12_CreateDeviceObjects();

	// Renderer
	gRenderer.Initialize();
	
	// Load Scene
	sLoadScene();

	// Features (rely on ImGui, Scene)
	gAtmosphere.Initialize();
	gCloud.Initialize();

	// File watch
	filewatch::FileWatch<std::string> file_watch("Shader/", 
		[] (const std::string& inPath, const filewatch::Event inChangeType) 
		{
			(void)inChangeType;
			std::regex pattern(".*\\.(hlsl|h|inl)");
			if (std::regex_match(inPath, pattern) && inChangeType == filewatch::Event::modified)
			{
				std::string msg = "Reload triggered by " + inPath + "\n";
				gTrace(msg.c_str());

				gRenderer.mReloadShader = true;
			}
		});

	gCommandList->Close();
	gCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&gCommandList));
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

		// New frame	
		gFrameIndex++;

		// Start the Dear ImGui frame
		ImGui_ImplDX12_FontTextureID = (ImTextureID)gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::ImGuiFont).ptr;
		ImGui_ImplDX12_NullTexture2D = (ImTextureID)gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::ImGuiNull2D).ptr;
		ImGui_ImplDX12_NullTexture3D = (ImTextureID)gGetFrameContext().mViewDescriptorHeap.GetGPUHandle(ViewDescriptorIndex::ImGuiNull3D).ptr;
		ImGui::GetIO().Fonts->SetTexID(ImGui_ImplDX12_FontTextureID);
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		sUpdate();
		sRender();
	}

	// Shutdown
	{
		sWaitForGPU();

		gAtmosphere.Finalize();
		gCloud.Finalize();

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		gScene.Unload();

		gRenderer.Finalize();

		sCleanupDeviceD3D();
		::DestroyWindow(hwnd);
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
	}

	return 0;
}

void sLoadCamera()
{
	int current_scene_index = static_cast<int>(sCurrentScene);

	gConstants.mCameraPosition = kScenePresets[current_scene_index].mCameraPosition;
	gConstants.mCameraDirection = glm::normalize(kScenePresets[current_scene_index].mCameraDirection);
	gCameraSettings.mHorizontalFovDegree = kScenePresets[current_scene_index].mHorizontalFovDegree;

	if (gScene.GetSceneContent().mCameraTransform.has_value())
	{
		gConstants.mCameraPosition = gScene.GetSceneContent().mCameraTransform.value()[3];
		gConstants.mCameraDirection = gScene.GetSceneContent().mCameraTransform.value()[2];
	}

	if (gScene.GetSceneContent().mFov.has_value())
		gCameraSettings.mHorizontalFovDegree = gScene.GetSceneContent().mFov.value();
}

void sLoadScene()
{
	int current_scene_index = static_cast<int>(sCurrentScene);

	gScene.Unload();
	gScene.Load(kScenePresets[current_scene_index].mPath, kScenePresets[current_scene_index].mTransform);
	gScene.Build();

	gConstants.mSunAzimuth = kScenePresets[current_scene_index].mSunAzimuth;
	gConstants.mSunZenith = kScenePresets[current_scene_index].mSunZenith;

	gAtmosphere.mProfile.mMode = kScenePresets[current_scene_index].mAtmosphere;
	if (gScene.GetSceneContent().mAtmosphereMode.has_value())
	{
		gAtmosphere.mProfile.mMode = gScene.GetSceneContent().mAtmosphereMode.value();
		gAtmosphere.mProfile.mConstantColor = gScene.GetSceneContent().mBackgroundColor;
	}

	sLoadCamera();
}

void sRender()
{
	// Frame Context
	sWaitForFrameContext();
	FrameContext& frame_context = gGetFrameContext();
	glm::uint32 frame_index = gSwapChain->GetCurrentBackBufferIndex();
	ID3D12Resource* back_buffer = gRenderer.mRuntime.mBackBuffers[frame_index].Get();
	D3D12_CPU_DESCRIPTOR_HANDLE& back_buffer_RTV = gRenderer.mRuntime.mBufferBufferRTVs[frame_index];

	if (DX12_ENABLE_PIX_CAPTURE)
	{
		if (gFrameIndex == 1) // PIXGpuCaptureNextFrames will capture the second frame, so skip the first one
			goto SkipRender;
	}

	// Frame Begin
	{
		frame_context.mCommandAllocator->Reset();
		gCommandList->Reset(frame_context.mCommandAllocator.Get(), nullptr);
	}

	// Update Scene
	{
		if (sPreviousScene != sCurrentScene)
		{
			sPreviousScene = sCurrentScene;

			sWaitForGPU();
			sLoadScene();

			gRenderer.mAccumulationResetRequested = true;
		}
	}

	// Update and Upload Constants
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Upload");

		{
			gConstants.mEV100			= glm::log2((gCameraSettings.mExposureControl.mAperture * gCameraSettings.mExposureControl.mAperture) / (1.0f / gCameraSettings.mExposureControl.mInvShutterSpeed) * 100.0f / gCameraSettings.mExposureControl.mSensitivity);
			gConstants.mSunDirection	= glm::vec4(0,1,0,0) * glm::rotate(gConstants.mSunZenith, glm::vec3(0, 0, 1)) * glm::rotate(gConstants.mSunAzimuth + glm::pi<float>() / 2.0f, glm::vec3(0, 1, 0));
			gConstants.mLightCount		= (glm::uint)gScene.GetSceneContent().mLights.size();

			if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				gConstants.mPixelDebugCoord	= glm::uvec2(static_cast<glm::uint32>(ImGui::GetMousePos().x), (glm::uint32)ImGui::GetMousePos().y);
		}

		// Accumulation
		{
			static Constants sConstantsCopy = gConstants;

			// Whitelist
			sConstantsCopy.mTime					= gConstants.mTime;
			sConstantsCopy.mCurrentFrameIndex		= gConstants.mCurrentFrameIndex;
			sConstantsCopy.mCurrentFrameWeight		= gConstants.mCurrentFrameWeight;
			sConstantsCopy.mPixelDebugCoord			= gConstants.mPixelDebugCoord;
			sConstantsCopy.mPixelDebugMode			= gConstants.mPixelDebugMode;

			if (memcmp(&sConstantsCopy, &gConstants, sizeof(Constants)) != 0)
				gRenderer.mAccumulationResetRequested = true;

			if (gRenderer.mAccumulationResetRequested)
			{
				gConstants.mCurrentFrameIndex = 0;
				gRenderer.mAccumulationDone = false;
			}

			if (gRenderer.mAccumulationDone || gRenderer.mAccumulationPaused)
				gConstants.mCurrentFrameWeight = 0.0f;
			else
				gConstants.mCurrentFrameWeight = 1.0f / (gConstants.mCurrentFrameIndex + 1);
			
			sConstantsCopy = gConstants;
			gRenderer.mAccumulationResetRequested = false;
		}
		
		memcpy(frame_context.mConstantUploadBufferPointer, &gConstants, sizeof(gConstants));

		{
			if (!gRenderer.mAccumulationPaused)
				gConstants.mCurrentFrameIndex++;

			glm::uint32 accumulation_frame_count = gRenderer.mAccumulationFrameUnlimited ? UINT_MAX : gRenderer.mAccumulationFrameCount;

			if (gConstants.mCurrentFrameIndex == accumulation_frame_count)
				gRenderer.mAccumulationDone = true;

			gConstants.mCurrentFrameIndex = gMin(gConstants.mCurrentFrameIndex, accumulation_frame_count - 1);
		}

		gBarrierTransition(gCommandList, gConstantBuffer.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
		gCommandList->CopyResource(gConstantBuffer.Get(), frame_context.mConstantUploadBuffer.Get());
		gBarrierTransition(gCommandList, gConstantBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	}

	// Scene
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Renderer");

		gRenderer.Render();
	}

	// Scene
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Scene");

		gScene.Render();
	}

	// Atmosphere
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Atmosphere");

		gAtmosphere.Render();
	}

	// Cloud
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Cloud");

		gCloud.Render();
	}

	// Clear
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Clear");

		gRenderer.Setup(gRenderer.mRuntime.mClearShader.mData);
		gCommandList->Dispatch((Debug::kValueArraySize + 63) / 64, 1, 1);

		gBarrierUAV(gCommandList, nullptr);
	}

	// Raytrace
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Raytrace");

		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
		gSwapChain->GetDesc1(&swap_chain_desc);

		gRenderer.Setup(gRenderer.mRuntime.mRayQueryShader.mData);
		gCommandList->Dispatch((swap_chain_desc.Width + 7) / 8, (swap_chain_desc.Height + 7) / 8, 1);

		gBarrierUAV(gCommandList, nullptr);
	}

	// Composite
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Composite");

		gBarrierTransition(gCommandList, back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		// Draw
		D3D12_RESOURCE_DESC desc = back_buffer->GetDesc();
		D3D12_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>(desc.Width);
		viewport.Height = static_cast<float>(desc.Height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		gCommandList->RSSetViewports(1, &viewport);
		D3D12_RECT rect = {};
		rect.left = 0;
		rect.top = 0;
		rect.right = static_cast<LONG>(desc.Width);
		rect.bottom = static_cast<LONG>(desc.Height);
		gCommandList->RSSetScissorRects(1, &rect);
		gCommandList->OMSetRenderTargets(1, &back_buffer_RTV, false, nullptr);
		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		gRenderer.Setup(gRenderer.mRuntime.mCompositeShader);
		gCommandList->DrawInstanced(3, 1, 0, 0);
	}

	// Readback
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Readback");

		gBarrierTransition(gCommandList, gDebugBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
		gCommandList->CopyResource(frame_context.mDebugReadbackBuffer.Get(), gDebugBuffer.Get());
		gBarrierTransition(gCommandList, gDebugBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
	}

SkipRender:

	// Draw ImGui
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "ImGui");

		sPrepareImGui(); // Keep this right before render to get latest data

		ImGui::Render();

		gCommandList->OMSetRenderTargets(1, &back_buffer_RTV, false, nullptr);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);
	}

	// Frame End
	{
		gBarrierTransition(gCommandList, back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		gCommandList->Close();
		gCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&gCommandList));

		gConstants.mTime += ImGui::GetIO().DeltaTime;
	}

	// Dump Texture
	{
		if (gDumpTexture != nullptr && gDumpTexture->mResource != nullptr)
		{
			DirectX::ScratchImage image;
			DirectX::CaptureTexture(gCommandQueue, gDumpTexture->mResource.Get(), false, image, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);

			std::filesystem::path path = gCreateDumpFolder();
			path += gDumpTexture->mName;
			path += ".dds";
			DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, path.c_str());

			gDumpTexture = nullptr;
		}
	}

	// Present
	{
		if (gDisplaySettings.mVsync)
			gSwapChain->Present(1, 0);
 		else
			gSwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

		UINT64 fence_value = gFenceLastSignaledValue + 1;
		gCommandQueue->Signal(gIncrementalFence, fence_value);
		gFenceLastSignaledValue = fence_value;
		frame_context.mFenceValue = fence_value;
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
	gAssert(false);
}

// Helper functions
static bool sCreateDeviceD3D(HWND hWnd)
{
	if (DX12_ENABLE_PIX_CAPTURE)
	{
		gPIXHandle = PIXLoadLatestWinPixGpuCapturerLibrary();
		PIXGpuCaptureNextFrames(L"Dump/pix.wpix", 1);
	}

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

	// InfoQueue callback
	if (DX12_ENABLE_INFO_QUEUE_CALLBACK)
	{
		ComPtr<ID3D12InfoQueue1> info_queue;
		if (SUCCEEDED(gDevice->QueryInterface(IID_PPV_ARGS(&info_queue))))
		{
			DWORD cookie = 0;
			if (FAILED(info_queue->RegisterMessageCallback(sMessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie)))
				return false;
		}
	}

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
			gRenderer.mRuntime.mBufferBufferRTVs[i] = rtvHandle;
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

	// Constants
	for (glm::uint i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
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

			D3D12_SAMPLER_DESC sampler_descs[] =
			{
				{.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP, .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP, .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP, .MipLODBias = 0, .MaxAnisotropy = 0, .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS, .BorderColor = {0,0,0,0}, .MinLOD = 0, .MaxLOD = D3D12_FLOAT32_MAX },
				{.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP, .MipLODBias = 0, .MaxAnisotropy = 0, .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS, .BorderColor = {0,0,0,0}, .MinLOD = 0, .MaxLOD = D3D12_FLOAT32_MAX },
			};
			static_assert(ARRAYSIZE(sampler_descs) == (int)SamplerDescriptorIndex::Count);
			for (int sampler_index = 0; sampler_index < (int)SamplerDescriptorIndex::Count; sampler_index++)
				gDevice->CreateSampler(&sampler_descs[sampler_index], gFrameContexts[i].mSamplerDescriptorHeap.GetHandle((SamplerDescriptorIndex)sampler_index));
		}

		// UploadBuffer
		{
			D3D12_RESOURCE_DESC resource_desc = gGetBufferResourceDesc(gAlignUp(static_cast<UINT>(sizeof(Constants)), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gFrameContexts[i].mConstantUploadBuffer)));
			name = L"FrameContext.ConstantUploadBuffer_" + std::to_wstring(i);
			gFrameContexts[i].mConstantUploadBuffer->SetName(name.c_str());

			// Persistent map - https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map#advanced-usage-models
			gFrameContexts[i].mConstantUploadBuffer->Map(0, nullptr, (void**)&gFrameContexts[i].mConstantUploadBufferPointer);
		}

		// ReadbackBuffer
		{
			D3D12_RESOURCE_DESC resource_desc = gGetBufferResourceDesc(gAlignUp(static_cast<UINT>(sizeof(Debug)), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			D3D12_HEAP_PROPERTIES props = gGetReadbackHeapProperties();

			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gFrameContexts[i].mDebugReadbackBuffer)));
			name = L"FrameContext.DebugReadbackBuffer_" + std::to_wstring(i);
			gFrameContexts[i].mDebugReadbackBuffer->SetName(name.c_str());

			// Persistent map - https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map#advanced-usage-models
			gFrameContexts[i].mDebugReadbackBuffer->Map(0, nullptr, (void**)&gFrameContexts[i].mDebugReadbackBufferPointer);
		}
	}

	// Constants (GPU)
	{
		// Buffer
		{
			D3D12_RESOURCE_DESC resource_desc = gGetBufferResourceDesc(gAlignUp(static_cast<UINT>(sizeof(Constants)), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&gConstantBuffer)));
			std::wstring name = L"ConstantBuffer";
			gConstantBuffer->SetName(name.c_str());

			D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
			desc.SizeInBytes = static_cast<UINT>(resource_desc.Width);
			desc.BufferLocation = gConstantBuffer->GetGPUVirtualAddress();
			for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
				gDevice->CreateConstantBufferView(&desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(ViewDescriptorIndex::Constants));
		}

		{
			D3D12_RESOURCE_DESC resource_desc = gGetBufferResourceDesc(gAlignUp(static_cast<UINT>(sizeof(Debug)), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
			D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

			resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&gDebugBuffer)));
			std::wstring name = L"DebugBuffer";
			gDebugBuffer->SetName(name.c_str());			

			D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			desc.Buffer.NumElements = 1;
			desc.Buffer.StructureByteStride = sizeof(Debug);
			desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
			for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
				gDevice->CreateUnorderedAccessView(gDebugBuffer.Get(), nullptr, &desc, gFrameContexts[i].mViewDescriptorHeap.GetHandle(ViewDescriptorIndex::BufferDebugUAV));
		}
	}

	gValidate(gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gFrameContexts[0].mCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&gCommandList)));
	gCommandList->SetName(L"gCommandList");

	if (gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gIncrementalFence)) != S_OK)
		return false;
	gIncrementalFence->SetName(L"gIncrementalFence");

	gIncrementalFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (gIncrementalFenceEvent == nullptr)
		return false;

	{
		ComPtr<IDXGIFactory4> dxgi_factory = nullptr;
		ComPtr<IDXGISwapChain1> swap_chain = nullptr;

		UINT flags = 0;
		if (DX12_ENABLE_DEBUG_LAYER)
			flags = DXGI_CREATE_FACTORY_DEBUG;

		if (CreateDXGIFactory2(flags, IID_PPV_ARGS(&dxgi_factory)) != S_OK ||
			dxgi_factory->CreateSwapChainForHwnd(gCommandQueue, hWnd, &sd, nullptr, nullptr, &swap_chain) != S_OK ||
			swap_chain->QueryInterface(IID_PPV_ARGS(&gSwapChain)) != S_OK)
			return false;

		// Fullscreen -> Windowed cause crash on resource reference in WM_SIZE handling, disable fullscreen for now
		dxgi_factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

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
		gFrameContexts[i].Reset();

	gConstantBuffer = nullptr;
	gDebugBuffer = nullptr;
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

	if (gPIXHandle != nullptr)
		FreeLibrary(gPIXHandle);
}

static void sWaitForGPU()
{
	gIncrementalFence->SetEventOnCompletion(gFenceLastSignaledValue, gIncrementalFenceEvent);
	WaitForSingleObject(gIncrementalFenceEvent, INFINITE);
}

static void sWaitForFrameContext()
{
	HANDLE waitableObjects[] = { gSwapChainWaitableObject, nullptr };
	DWORD numWaitableObjects = 1;

	FrameContext& frame_context = gGetFrameContext();
	UINT64 fenceValue = frame_context.mFenceValue;
	if (fenceValue != 0) // means no fence was signaled
	{
		gIncrementalFence->SetEventOnCompletion(frame_context.mFenceValue, gIncrementalFenceEvent);
		frame_context.mFenceValue = 0;
		waitableObjects[1] = gIncrementalFenceEvent;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);
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

			gRenderer.InitializeScreenSizeTextures();
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
