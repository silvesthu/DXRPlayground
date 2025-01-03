#include "Common.h"

#include "Renderer.h"

#include "Color.h"
#include "Scene.h"

#include "Atmosphere.h"
#include "Cloud.h"

#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx12.h"
#include "ImGuizmo/ImGuizmoExt.h"

#pragma warning(push)
#pragma warning(disable: 4068)
#include "Thirdparty/filewatch/FileWatch.hpp"
#pragma warning(pop)

extern "C" { __declspec(dllexport) extern const UINT			D3D12SDKVersion = 614; }
extern "C" { __declspec(dllexport) extern const char8_t*		D3D12SDKPath = u8".\\D3D12\\"; }

#define DX12_ENABLE_DEBUG_LAYER			(0)
#define DX12_ENABLE_GBV					(0)
#define DX12_ENABLE_INFO_QUEUE_CALLBACK (0)
#define DX12_ENABLE_PIX_CAPTURE			(0)

static const wchar_t*											kApplicationTitleW = L"DXR Playground";
static const std::wstring										kINIPathStringW = std::filesystem::absolute(L"DXRPlayground.ini").wstring();
static const wchar_t*											kINIPathW = kINIPathStringW.c_str();

struct ScenePreset
{
#define SCENE_PRESET_MEMBER(type, name, default_value) MEMBER(ScenePreset, type, name, default_value)

	SCENE_PRESET_MEMBER(std::string_view, 	Name, 					"");
	SCENE_PRESET_MEMBER(std::string_view, 	Path, 					"");
	SCENE_PRESET_MEMBER(glm::vec4, 			CameraPosition, 		glm::vec4(0, 1, 0, 1));
	SCENE_PRESET_MEMBER(glm::vec4, 			CameraDirection, 		glm::vec4(0, 0, -1, 0));
	SCENE_PRESET_MEMBER(LightSourceMode,	LightSource, 			LightSourceMode::Emitter);
	SCENE_PRESET_MEMBER(float, 				EmissionBoost, 			1.0f);														// As no auto exposure yet
	SCENE_PRESET_MEMBER(float, 				HorizontalFovDegree, 	90);
	SCENE_PRESET_MEMBER(glm::mat4x4, 		Transform, 				glm::mat4x4(1));
	SCENE_PRESET_MEMBER(float, 				SunAzimuth, 			0);
	SCENE_PRESET_MEMBER(float, 				SunZenith, 				glm::pi<float>() / 4.0f);
	SCENE_PRESET_MEMBER(AtmosphereMode,		Atmosphere,				AtmosphereMode::ConstantColor);
};

static const std::array kScenePresets =
{
	ScenePreset().Name("None"),

	// Basics
	ScenePreset().Name("CornellBox").Path("Asset/Comparison/benedikt-bitterli/cornell-box/scene_v3.xml").EmissionBoost(1E4f),
	ScenePreset().Name("CornellBoxDielectric").Path("Asset/Comparison/benedikt-bitterli/cornell-box-dielectric/scene_v3.xml").EmissionBoost(1E4f),
	ScenePreset().Name("CornellBoxTeapot").Path("Asset/Comparison/benedikt-bitterli/cornell-box-teapot/scene_v3.xml").EmissionBoost(1E4f),
	ScenePreset().Name("CornellMonkey").Path("Asset/Comparison/benedikt-bitterli/cornell-box-monkey/scene_v3.xml").EmissionBoost(1E4f),

	// MIS
	ScenePreset().Name("VeachMIS").Path("Asset/Comparison/benedikt-bitterli/veach-mis/scene_ggx_v3.xml").EmissionBoost(1E4f),

	// RIS and ReSTIR
	ScenePreset().Name("VeachMISManyLight").Path("Asset/Comparison/benedikt-bitterli/veach-mis-manylight/scene_ggx_v3.xml").EmissionBoost(1E4f),
	ScenePreset().Name("Arcade").Path("Asset/Comparison/RTXDI/Arcade/Arcade.gltf").CameraPosition(glm::vec4(-1.658f, 1.577f, 1.69f, 0.0f)).CameraDirection(glm::vec4(-0.9645f, 1.2672f, 1.0396f, 0.0f) - glm::vec4(-1.658f, 1.577f, 1.69f, 0.0f)).EmissionBoost(1E6f),

	// Simple scenes
	ScenePreset().Name("Dragon").Path("Asset/Comparison/benedikt-bitterli/dragon/scene_v3.xml").EmissionBoost(1E4f),
	
	// Complex scenes
	ScenePreset().Name("LivingRoom2").Path("Asset/Comparison/benedikt-bitterli/living-room-2/scene_v3.xml").EmissionBoost(1E4f),
	ScenePreset().Name("VeachAjar").Path("Asset/Comparison/benedikt-bitterli/veach-ajar/scene_v3.xml").EmissionBoost(1E4f),
	ScenePreset().Name("VeachBidir").Path("Asset/Comparison/benedikt-bitterli/veach-bidir/scene_v3.xml").EmissionBoost(1E4f),
	
	// Atmosphere
	ScenePreset().Name("Bruneton17").Path("Asset/primitives/sphere.obj").CameraPosition(glm::vec4(0.0f, 0.0f, 9.0f, 0.0f)).CameraDirection(glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)).Transform(glm::translate(glm::vec3(0.0f, 1.0f, 0.0f))).Atmosphere(AtmosphereMode::Bruneton17),
	ScenePreset().Name("Bruneton17_Artifact_Mu").Path("Asset/primitives/sphere.obj").CameraPosition(glm::vec4(0.0f, 80.0f, 150.0f, 0.0f)).CameraDirection(glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)).Transform(glm::scale(glm::vec3(100.0f, 100.0f, 100.0f))).Atmosphere(AtmosphereMode::Bruneton17),
	ScenePreset().Name("Hillaire20").CameraPosition(glm::vec4(0.0f, 0.5, -1.0f, 0.0f)).CameraDirection(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)).HorizontalFovDegree(98.8514328f).Transform(glm::translate(glm::vec3(0.0f, 1.0f, 0.0f))).SunZenith(glm::pi<float>() / 2.0f - 0.45f).Atmosphere(AtmosphereMode::Hillaire20),

	// IES
	ScenePreset().Name("IES").Path("Asset/IES/007cfb11e343e2f42e3b476be4ab684e/scene_v3.xml"),
};
const ScenePreset& sFindScenePreset(const std::string_view inName)
{
	auto iter = std::find_if(kScenePresets.begin(), kScenePresets.end(), [&inName](const ScenePreset& inPreset) { return inPreset.mName == inName; });
	if (iter == kScenePresets.end())
		return kScenePresets.front();
	else
		return *iter;
}
int sFindScenePresetIndex(const std::string_view inName)
{
	return static_cast<int>(&sFindScenePreset(inName) - &kScenePresets.front());
}
static int sCurrentSceneIndex = sFindScenePresetIndex("VeachMISManyLight");
static int sPreviousSceneIndex = sCurrentSceneIndex;

struct CameraSettings
{
	float			mMoveSpeed = 0.1f;
	float			mRotateSpeed = 0.01f;
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
	gCPUContext.mDumpTextureProxy.mResource = gRenderer.mRuntime.mScreenColorTexture.mResource;
	gCPUContext.mDumpTextureProxy.mName = "Luminance";
	gCPUContext.mDumpTextureRef = &gCPUContext.mDumpTextureProxy;
}
static void sPrepareImGui();
static void sPrepareImGuizmo();
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

			if (ImGui::Button("Copy Mitsuba Camera"))
			{
				glm::mat4x4 camera_transform = gConstants.mCameraTransform;
				camera_transform[0] = -camera_transform[0];
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
				gOpenSceneFolder(kScenePresets[sCurrentSceneIndex].mPath);
		}

		if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputInt2("Coords", (int*)&gConstants.mPixelDebugCoord);
			ImGui::SliderInt("Light Index", &gConstants.mPixelDebugLightIndex, 0, (int)gScene.GetSceneContent().mLights.size() - 1);
			ImGui::InputFloat3("Pixel Value", &gRenderer.mRuntime.mPixelInspectionBuffer.ReadbackAs<PixelInspection>(gGetFrameContextIndex())->mPixelValue.x, "%.8f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputFloat3("Debug Value", &gRenderer.mRuntime.mPixelInspectionBuffer.ReadbackAs<PixelInspection>(gGetFrameContextIndex())->mDebugValue.x, "%.8f", ImGuiInputTextFlags_ReadOnly);
			ImGui::SliderInt("Recursion", &gConstants.mPixelDebugRecursion, 0, 16);
			if (ImGui::TreeNodeEx("DebugValue For Each Recursion"))
			{
				for (int i = 0; i < static_cast<int>(PixelDebugMode::Count); i++)
				{
					const auto& name = nameof::nameof_enum(static_cast<PixelDebugMode>(i));
					if (name.starts_with('_'))
					{
						ImGui::NewLine();
						continue;
					}

					if (i != 0)
						ImGui::SameLine();

					ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mPixelDebugMode), i);
				}

				for (int i = 0; i < PixelInspection::kArraySize; i++)
					ImGui::InputFloat4(std::to_string(i).c_str(), &gRenderer.mRuntime.mPixelInspectionBuffer.ReadbackAs<PixelInspection>(gGetFrameContextIndex())->mPixelValueArray[i].x, "%.8f", ImGuiInputTextFlags_ReadOnly);

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

			ImGui::Text("Light Source Mode");
			for (int i = 0; i < static_cast<int>(LightSourceMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<LightSourceMode>(i));
				ImGui::SameLine();
				ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mLightSourceMode), i);
			}

			ImGui::Text("Light Sample Mode");
			for (int i = 0; i < static_cast<int>(LightSampleMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<LightSampleMode>(i));
				ImGui::SameLine();
				ImGui::RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mLightSampleMode), i);
			}

			if (gConstants.mLightSampleMode == LightSampleMode::ReSTIR)
			{
				ImGui::SliderInt("Initial Sample Count", reinterpret_cast<int*>(&gConstants.mReSTIR.mInitialSampleCount), 1, 32);
			}

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Accumulation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Checkbox("Frame Count Unlimited", &gRenderer.mAccumulationFrameUnlimited))
				gRenderer.mAccumulationResetRequested = true;
			ImGui::SameLine();
			ImGui::Checkbox("Paused", &gRenderer.mAccumulationPaused);
			ImGui::SameLine();
			ImGui::Checkbox("Vsync", &gDisplaySettings.mVsync);

			if (!gRenderer.mAccumulationFrameUnlimited)
				if (ImGui::SliderInt("Frame Count", reinterpret_cast<int*>(&gRenderer.mAccumulationFrameCount), 1, 64))
					gRenderer.mAccumulationResetRequested = true;

			ImGui::SliderInt("Recursion Depth Max", reinterpret_cast<int*>(&gConstants.mRecursionDepthCountMax), 1, 16);
			ImGui::SliderInt("Russian Roulette Depth", reinterpret_cast<int*>(&gConstants.mRussianRouletteDepth), 1, 16);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Camera"))
		{
			auto align_right = [](float pivot = ImGui::GetCursorPosX()) { ImGui::SetNextItemWidth(ImGui::GetWindowWidth() * 0.65f - (ImGui::GetCursorPosX() - pivot)); };

			ImGui::InputFloat3("Position", (float*)&gConstants.mCameraTransform[3]);
			ImGui::InputFloat3("Direction", (float*)&gConstants.mCameraTransform[2], "%.3f", ImGuiInputTextFlags_ReadOnly);
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
				std::string format = "%.3f";
				if (gCameraSettings.mExposureControl.mInvShutterSpeed < 1.0f)
					format += std::format(" ({:.1f}sec)", 1.0f / gCameraSettings.mExposureControl.mInvShutterSpeed);
				align_right(x); ImGui::SliderFloat("Shutter Speed (1/sec)", &gCameraSettings.mExposureControl.mInvShutterSpeed, 1.0f, 500.0f, format.c_str());
			}
			ImGui::PopID();
			ImGui::PushID("ISO");
			{
				float x = ImGui::GetCursorPosX();

				if (ImGui::Button("<")) { gCameraSettings.mExposureControl.mSensitivity /= 2.0f; }
				ImGui::SameLine();
				if (ImGui::Button(">")) { gCameraSettings.mExposureControl.mSensitivity *= 2.0f; }
				ImGui::SameLine();
				align_right(x); ImGui::SliderFloat("ISO", &gCameraSettings.mExposureControl.mSensitivity, 100.0f, 3200.0f);
			}
			ImGui::PopID();

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

			ImGui::SliderFloat("Emission Boost", &gConstants.mEmissionBoost, 1E-16f, 1E16F);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Render"))
		{
			for (int i = 0; i < static_cast<int>(DebugMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<DebugMode>(i));
				if (name.starts_with('_'))
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
			for (int i = 0; i < kScenePresets.size(); i++)
			{
				if (std::filesystem::exists(kScenePresets[i].mPath))
					ImGui::RadioButton(kScenePresets[i].mName.data(), &sCurrentSceneIndex, i);
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
			if (ImGui::Button("Dump RayQuery"))
			{
				gRenderer.mDumpRayQuery = true;
				gRenderer.mReloadShader = true;
			}

			if (ImGui::Checkbox("Test Lib Shader", &gRenderer.mTestLibShader))
				gRenderer.mAccumulationResetRequested = true;

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Display"))
		{
			ImGui::Checkbox("Vsync", &gDisplaySettings.mVsync);

			if (ImGui::Button("1280 x 720"))
				gRenderer.Resize(1280, 720);

			if (ImGui::Button("1920 x 1080"))
				gRenderer.Resize(1920, 1080);

			if (ImGui::Button("2560 x 1440"))
				gRenderer.Resize(2560, 1440);

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
					if (name.starts_with('_'))
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
					"BSDF",
					"Albedo",
					"Reflectance",
					"Transmittance",
					"Eta",
					"K",
					"Emission",
					"RoughnessAlpha",
					"Opacity",
					"VertexCount",
					"PrimitiveCount",
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
						if (ImGui::Selectable(std::to_string(row).c_str(), row == gConstants.mDebugInstanceIndex, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnNav))
							gConstants.mDebugInstanceIndex = row;

						if (row == gRenderer.mRuntime.mPixelInspectionBuffer.ReadbackAs<PixelInspection>(gGetFrameContextIndex())->mPixelInstanceID)
							ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.8f, 0.2f, 0.2f, 0.8f)));

						ImGui::TableSetColumnIndex(column_index++);
						ImGui::Text("%s", instance_info.mName.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string position = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mTransform[3][0], instance_data.mTransform[3][1], instance_data.mTransform[3][2]);
						position = glm::dot(glm::vec3(instance_data.mTransform[3]), glm::vec3(instance_data.mTransform[3])) != 0.0f ? position : "";
						ImGui::Text(position.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						ImGui::Text("%s%s", NAMEOF_ENUM(instance_data.mBSDF).data(), instance_data.mTwoSided ? " (TwoSided)" : "");

						ImGui::TableSetColumnIndex(column_index++);
						std::string albedo = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mAlbedo.x, instance_data.mAlbedo.y, instance_data.mAlbedo.z);
						albedo = instance_data.mAlbedo != InstanceData().mAlbedo ? albedo : "";
						albedo = instance_info.mAlbedoTexture.empty() ? albedo : (instance_info.mAlbedoTexture.filename().string() + " (" + std::to_string(instance_data.mAlbedoTexture.mTextureIndex) + ", " + std::to_string(instance_data.mAlbedoTexture.mSamplerIndex) + ")");
						ImGui::Text(albedo.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string reflectance = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mReflectance.x, instance_data.mReflectance.y, instance_data.mReflectance.z);
						reflectance = instance_data.mReflectance != InstanceData().mReflectance ? reflectance : "";
						reflectance = instance_info.mReflectanceTexture.empty() ? reflectance : (instance_info.mReflectanceTexture.filename().string() + " (" + std::to_string(instance_data.mReflectanceTexture.mTextureIndex) + ", " + std::to_string(instance_data.mReflectanceTexture.mSamplerIndex) + ")");
						ImGui::Text(reflectance.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string transmittance = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mSpecularTransmittance.x, instance_data.mSpecularTransmittance.y, instance_data.mSpecularTransmittance.z);
						transmittance = instance_data.mSpecularTransmittance != InstanceData().mSpecularTransmittance ? transmittance : "";
						ImGui::Text(transmittance.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string eta = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mEta.x, instance_data.mEta.y, instance_data.mEta.z);
						eta = instance_data.mEta != InstanceData().mEta ? eta : "";
						ImGui::Text(eta.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string k = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mK.x, instance_data.mK.y, instance_data.mK.z);
						k = instance_data.mK != InstanceData().mK ? k : "";
						ImGui::Text(k.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string emission = std::format("{:.2f} {:.2f} {:.2f}", instance_data.mEmission.x, instance_data.mEmission.y, instance_data.mEmission.z);
						emission = instance_data.mEmission != InstanceData().mEmission ? emission : "";
						emission = instance_info.mEmissionTexture.empty() ? emission : (instance_info.mEmissionTexture.filename().string() + " (" + std::to_string(instance_data.mEmissionTexture.mTextureIndex) + ", " + std::to_string(instance_data.mEmissionTexture.mSamplerIndex) + ")");
						ImGui::Text(emission.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string roughness_alpha = std::format("{:.2f}", instance_data.mRoughnessAlpha);
						roughness_alpha = instance_data.mRoughnessAlpha != InstanceData().mRoughnessAlpha ? roughness_alpha : "";
						ImGui::Text(roughness_alpha.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string opacity = std::format("{:.2f}", instance_data.mOpacity);
						ImGui::Text(opacity.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string vertex_count = std::format("{}", instance_data.mVertexCount);
						ImGui::Text(vertex_count.c_str());

						ImGui::TableSetColumnIndex(column_index++);
						std::string index_count = std::format("{}", instance_data.mIndexCount / kIndexCountPerTriangle);
						ImGui::Text(index_count.c_str());

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
						if (ImGui::Selectable(std::to_string(row).c_str(), row == gConstants.mDebugLightIndex, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnNav))
							gConstants.mDebugLightIndex = row;

						if (static_cast<int>(light.mInstanceID) == gRenderer.mRuntime.mPixelInspectionBuffer.ReadbackAs<PixelInspection>(gGetFrameContextIndex())->mPixelInstanceID)
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

			if (ImGui::Begin("Stats"))
			{
				ImGui::InputInt("Instruction Count", &gStats.mInstructionCount, 0, 0, ImGuiInputTextFlags_ReadOnly);
				ImGui::InputFloat("Time (ms)", &gStats.mTimeInMS, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
			}
			ImGui::End();
		}
	}
	ImGui::End();
}

void sPrepareImGuizmo()
{
	ImGuiIO& io = ImGui::GetIO();
	ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

	float axis_length = 48.0f;
	float2 axis_center = float2(64.0f, io.DisplaySize.y - 64.0f);
	glm::vec4 x_vector = gConstants.mViewMatrix * glm::vec4(axis_length, 0, 0, 0);
	glm::vec4 y_vector = gConstants.mViewMatrix * glm::vec4(0, axis_length, 0, 0);
	glm::vec4 z_vector = gConstants.mViewMatrix * glm::vec4(0, 0, axis_length, 0);
	float2 x_axis = axis_center + float2(x_vector.x, -x_vector.y);
	float2 y_axis = axis_center + float2(y_vector.x, -y_vector.y);
	float2 z_axis = axis_center + float2(z_vector.x, -z_vector.y);
	float2 xz_axis = axis_center + (float2(x_vector.x, -x_vector.y) + float2(z_vector.x, -z_vector.y)) / float2(glm::sqrt(2.0f));
	ImGuizmo::GetDrawlist()->AddLine(axis_center, x_axis, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f)), 5.0f);
	ImGuizmo::GetDrawlist()->AddLine(axis_center, y_axis, ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 1.0f, 0.0f, 1.0f)), 5.0f);
	ImGuizmo::GetDrawlist()->AddLine(axis_center, z_axis, ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 1.0f, 1.0f)), 5.0f);
	ImGuizmo::GetDrawlist()->AddLine(axis_center, xz_axis, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.0f, 1.0f, 1.0f)), 2.0f);
}

static void sUpdate()
{
	// Resize
	if (gRenderer.mResizeWidth != 0)
	{
		RECT rect = { 0, 0, (LONG)gRenderer.mResizeWidth, (LONG)gRenderer.mResizeHeight };
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
	{
		float frame_speed_scale = ImGui::GetIO().DeltaTime / (1.0f / 60.0f);
		float move_speed = gCameraSettings.mMoveSpeed * frame_speed_scale;
		if (ImGui::GetIO().KeyShift)
			move_speed *= 20.0f;
		if (ImGui::GetIO().KeyCtrl)
			move_speed *= 0.1f;

		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_W))
			gConstants.CameraPosition() += gConstants.CameraFront() * move_speed;
		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_S))
			gConstants.CameraPosition() -= gConstants.CameraFront() * move_speed;

		glm::vec4 left = glm::vec4(glm::normalize(glm::cross(glm::vec3(0, 1, 0), glm::vec3(gConstants.CameraFront()))), 0);

		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_A))
			gConstants.CameraPosition() += left * move_speed;
		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_D))
			gConstants.CameraPosition() -= left * move_speed;

		glm::vec4 up = glm::vec4(glm::normalize(glm::cross(glm::vec3(left), glm::vec3(gConstants.CameraFront()))), 0);

		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_Q))
			gConstants.CameraPosition() += up * move_speed;
		if (ImGui::IsKeyDown(ImGuiKey::ImGuiKey_E))
			gConstants.CameraPosition() -= up * move_speed;

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

	// Setup matrices
	{
		gConstants.mScreenWidth					= gRenderer.mScreenWidth;
		gConstants.mScreenHeight				= gRenderer.mScreenHeight;

		float horizontal_fov_radian				= gCameraSettings.mHorizontalFovDegree * glm::pi<float>() / 180.0f;
		float horizontal_tan					= glm::tan(horizontal_fov_radian * 0.5f);
		float vertical_tan						= horizontal_tan * (gDisplaySettings.mRenderResolution.y * 1.0f / gDisplaySettings.mRenderResolution.x);
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
int WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, PSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
	// INI
	int window_x = GetPrivateProfileInt(L"Main", L"Window_X", 100, kINIPathW);
	int window_y = GetPrivateProfileInt(L"Main", L"Window_Y", 100, kINIPathW);
	
	// Create application window
	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, sWndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, kApplicationTitleW, nullptr };
	::RegisterClassEx(&wc);

	RECT rect = { 0, 0, 1920, 1080 };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
	HWND hwnd = ::CreateWindow(wc.lpszClassName, kApplicationTitleW, WS_OVERLAPPEDWINDOW, window_x, window_y, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, nullptr);

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
		ImGuizmo::BeginFrame();

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
	gConstants.CameraPosition()					= kScenePresets[sCurrentSceneIndex].mCameraPosition;
	gConstants.CameraFront()					= kScenePresets[sCurrentSceneIndex].mCameraDirection;
	gConstants.mEmissionBoost					= kScenePresets[sCurrentSceneIndex].mEmissionBoost;
	gCameraSettings.mHorizontalFovDegree		= kScenePresets[sCurrentSceneIndex].mHorizontalFovDegree;

	if (gScene.GetSceneContent().mCameraTransform.has_value())
	{
		gConstants.CameraPosition()				= gScene.GetSceneContent().mCameraTransform.value()[3];
		gConstants.CameraFront()				= gScene.GetSceneContent().mCameraTransform.value()[2];
	}

	gConstants.CameraFront()					= glm::normalize(gConstants.CameraFront());
	gConstants.CameraUp()						= float4(0, 1, 0, 0);
	gConstants.CameraLeft()						= float4(glm::cross(float3(gConstants.CameraUp()), float3(gConstants.CameraFront())), 0.0f);

	if (gScene.GetSceneContent().mFov.has_value())
		gCameraSettings.mHorizontalFovDegree = gScene.GetSceneContent().mFov.value();
}

void sLoadScene()
{
	gScene.Unload();
	gScene.Load(kScenePresets[sCurrentSceneIndex].mPath, kScenePresets[sCurrentSceneIndex].mTransform);
	gScene.Build();

	gConstants.mSunAzimuth = kScenePresets[sCurrentSceneIndex].mSunAzimuth;
	gConstants.mSunZenith = kScenePresets[sCurrentSceneIndex].mSunZenith;

	gAtmosphere.mProfile.mMode = kScenePresets[sCurrentSceneIndex].mAtmosphere;
	if (gScene.GetSceneContent().mAtmosphereMode.has_value())
	{
		gAtmosphere.mProfile.mMode = gScene.GetSceneContent().mAtmosphereMode.value();
		gAtmosphere.mProfile.mConstantColor = gScene.GetSceneContent().mBackgroundColor;
	}

	gRenderer.mReloadShader = true;

	sLoadCamera();
}

void sRender()
{
	// Frame Context
	sWaitForFrameContext();
	FrameContext& frame_context									= gGetFrameContext();
	uint32_t back_buffer_index									= gSwapChain->GetCurrentBackBufferIndex();
	ID3D12Resource* back_buffer									= gRenderer.mRuntime.mBackBuffers[back_buffer_index].mResource.Get();
	
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
		if (sPreviousSceneIndex != sCurrentSceneIndex)
		{
			sPreviousSceneIndex = sCurrentSceneIndex;

			sWaitForGPU();
			sLoadScene();

			gRenderer.mAccumulationResetRequested = true;
		}
	}

	// Update and Upload Constants
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Upload");

		{
			// https://google.github.io/filament/Filament.html#imagingpipeline/physicallybasedcamera/exposurevalue
			gConstants.mEV100			= glm::log2(
											(gCameraSettings.mExposureControl.mAperture * gCameraSettings.mExposureControl.mAperture) / 
											(1.0f / gCameraSettings.mExposureControl.mInvShutterSpeed) * 100.0f / gCameraSettings.mExposureControl.mSensitivity);
			gConstants.mSunDirection	= glm::vec4(0,1,0,0) * glm::rotate(gConstants.mSunZenith, glm::vec3(0, 0, 1)) * glm::rotate(gConstants.mSunAzimuth + glm::pi<float>() / 2.0f, glm::vec3(0, 1, 0));
			gConstants.mLightCount		= gConstants.mLightSourceMode != LightSourceMode::TriangleLights ? (glm::uint)gScene.GetSceneContent().mLights.size() : 0;

			if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
				gConstants.mPixelDebugCoord	= glm::uvec2(static_cast<uint32_t>(ImGui::GetMousePos().x), (uint32_t)ImGui::GetMousePos().y);
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

		{
			Constants gpu_constants = gConstants;
			memcpy(gRenderer.mRuntime.mConstantsBuffer.mUploadPointer[gGetFrameContextIndex()], &gpu_constants, sizeof(gConstants));
		}

		{
			if (!gRenderer.mAccumulationPaused)
				gConstants.mCurrentFrameIndex++;

			uint32_t accumulation_frame_count = gRenderer.mAccumulationFrameUnlimited ? UINT_MAX : gRenderer.mAccumulationFrameCount;

			if (gConstants.mCurrentFrameIndex == accumulation_frame_count)
				gRenderer.mAccumulationDone = true;

			gConstants.mCurrentFrameIndex = gMin(gConstants.mCurrentFrameIndex, accumulation_frame_count - 1);
		}

		gBarrierTransition(gCommandList, gRenderer.mRuntime.mConstantsBuffer.mResource.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
		gCommandList->CopyResource(gRenderer.mRuntime.mConstantsBuffer.mResource.Get(), gRenderer.mRuntime.mConstantsBuffer.mUploadResource[gGetFrameContextIndex()].Get());
		gBarrierTransition(gCommandList, gRenderer.mRuntime.mConstantsBuffer.mResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	}

	// Renderer
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

	// Texture Generator
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Texture Generator");

		gRenderer.Setup(gRenderer.mRuntime.mGenerateTextureShader);

		BarrierScope scope(gCommandList, gRenderer.mRuntime.mGeneratedTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gCommandList->Dispatch(gAlignUpDiv(gRenderer.mRuntime.mGeneratedTexture.mWidth, 8u), gAlignUpDiv(gRenderer.mRuntime.mGeneratedTexture.mHeight, 8u), 1);
		
		gBarrierUAV(gCommandList, nullptr);
	}

	// Clear
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Clear");

		gRenderer.Setup(gRenderer.mRuntime.mClearShader);
		gCommandList->Dispatch(gAlignUpDiv(PixelInspection::kArraySize, 64u), 1, 1);

		BarrierScope depth_scope(gCommandList, gRenderer.mRuntime.mScreenDebugTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gRenderer.ClearUnorderedAccessViewFloat(gRenderer.mRuntime.mScreenDebugTexture);

		gBarrierUAV(gCommandList, nullptr);
	}
	
	// Depth
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Depth");

		BarrierScope depth_scope(gCommandList, gRenderer.mRuntime.mScreenDepthTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);

		D3D12_VIEWPORT viewport =
		{
			.TopLeftX = 0.0f,
			.TopLeftY = 0.0f,
			.Width = static_cast<float>(gRenderer.mScreenWidth),
			.Height = static_cast<float>(gRenderer.mScreenHeight),
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f,
		};
		gCommandList->RSSetViewports(1, &viewport);
		D3D12_RECT rect =
		{
			.left = 0,
			.top = 0,
			.right = static_cast<LONG>(gRenderer.mScreenWidth),
			.bottom = static_cast<LONG>(gRenderer.mScreenHeight),
		};
		gCommandList->RSSetScissorRects(1, &rect);
		D3D12_CPU_DESCRIPTOR_HANDLE depth_cpu_handle = gCPUContext.mDSVDescriptorHeap.GetCPUHandle(gRenderer.mRuntime.mScreenDepthTexture.mDSVIndex);
		gCommandList->OMSetRenderTargets(0, nullptr, false, &depth_cpu_handle);
		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		gRenderer.Setup(gRenderer.mRuntime.mDepthShader);
		gCommandList->DrawInstanced(3, 1, 0, 0);

		gBarrierUAV(gCommandList, nullptr);
	}

	// PrepareLights
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "PrepareLights");

		gRenderer.Setup(gRenderer.mRuntime.mPrepareLightsShader);
		uint constants[] = { static_cast<uint>(gScene.GetPrepareLightsTaskCount()) };
		gCommandList->SetComputeRoot32BitConstants(static_cast<int>(RootParameterIndex::ConstantsPrepareLights), 1, &constants, 0);
		gCommandList->Dispatch(gAlignUpDiv(gScene.GetSceneContent().mEmissiveTriangleCount, 256u), 1, 1);

		gBarrierUAV(gCommandList, nullptr);
	}

	// RayQuery
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "RayQuery");

		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
		gSwapChain->GetDesc1(&swap_chain_desc);

		UINT64 timestamp = gTiming.TimestampBegin(gRenderer.mRuntime.mQueryBuffer.ReadbackAs<UINT64>(gGetFrameContextIndex()));
		gRenderer.Setup(gRenderer.mRuntime.mRayQueryShader);
		gCommandList->Dispatch(gAlignUpDiv(swap_chain_desc.Width, 8u), gAlignUpDiv(swap_chain_desc.Height, 8u), 1);
		gTiming.TimestampEnd(gRenderer.mRuntime.mQueryBuffer.ReadbackAs<UINT64>(gGetFrameContextIndex()), timestamp, gStats.mTimeInMS);

		gBarrierUAV(gCommandList, nullptr);
	}

	// Test Hit Shader
	if (gRenderer.mTestLibShader)
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Test Hit Shader");

		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc;
		gSwapChain->GetDesc1(&swap_chain_desc);

		D3D12_DISPATCH_RAYS_DESC dispatch_rays_desc = {};
		{
			dispatch_rays_desc.Width = swap_chain_desc.Width;
			dispatch_rays_desc.Height = swap_chain_desc.Height;
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
		gCommandList->DispatchRays(&dispatch_rays_desc);

		gBarrierUAV(gCommandList, nullptr);
	}

	// Composite
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Composite");

		gBarrierTransition(gCommandList, back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		BarrierScope depth_scope(gCommandList, gRenderer.mRuntime.mScreenDepthTexture.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_READ);

		D3D12_VIEWPORT viewport =
		{
			.TopLeftX = 0.0f,
			.TopLeftY = 0.0f,
			.Width = static_cast<float>(gRenderer.mScreenWidth),
			.Height = static_cast<float>(gRenderer.mScreenHeight),
			.MinDepth = 0.0f,
			.MaxDepth = 1.0f,
		};
		gCommandList->RSSetViewports(1, &viewport);
		D3D12_RECT rect =
		{
			.left = 0,
			.top = 0,
			.right = static_cast<LONG>(gRenderer.mScreenWidth),
			.bottom = static_cast<LONG>(gRenderer.mScreenHeight),
		};
		gCommandList->RSSetScissorRects(1, &rect);
		D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_cpu_handle = gCPUContext.mRTVDescriptorHeap.GetCPUHandle(gRenderer.mRuntime.mBackBuffers[back_buffer_index].mRTVIndex);
		D3D12_CPU_DESCRIPTOR_HANDLE depth_cpu_handle = gCPUContext.mDSVDescriptorHeap.GetCPUHandle(gRenderer.mRuntime.mScreenDepthTexture.mDSVIndex);
		gCommandList->OMSetRenderTargets(1, &back_buffer_cpu_handle, false, &depth_cpu_handle);

		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gRenderer.Setup(gRenderer.mRuntime.mCompositeShader);
		gCommandList->DrawInstanced(3, 1, 0, 0);

		gCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
		gRenderer.Setup(gRenderer.mRuntime.mLineShader);
		gCommandList->DrawInstanced(RayInspection::kArraySize, 1, 0, 0);
	}

	// Readback
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "Readback");

		gBarrierTransition(gCommandList, gRenderer.mRuntime.mPixelInspectionBuffer.mResource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
		gCommandList->CopyResource(gRenderer.mRuntime.mPixelInspectionBuffer.mReadbackResource[gGetFrameContextIndex()].Get(), gRenderer.mRuntime.mPixelInspectionBuffer.mResource.Get());
		gBarrierTransition(gCommandList, gRenderer.mRuntime.mPixelInspectionBuffer.mResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
	}

SkipRender:

	// Draw ImGui
	{
		PIXScopedEvent(gCommandList, PIX_COLOR(0, 255, 0), "ImGui");

		sPrepareImGui(); // Keep this right before render to get latest data

		sPrepareImGuizmo();

		ImGui::Render();

		D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_cpu_handle = gCPUContext.mRTVDescriptorHeap.GetCPUHandle(gRenderer.mRuntime.mBackBuffers[back_buffer_index].mRTVIndex);
		gCommandList->OMSetRenderTargets(1, &back_buffer_cpu_handle, false, nullptr);

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);
	}

	// Frame End
	{
		gTiming.FrameEnd(gRenderer.mRuntime.mQueryBuffer.mReadbackResource[gGetFrameContextIndex()].Get());

		gBarrierTransition(gCommandList, back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		gCommandList->Close();
		gCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&gCommandList));

		gConstants.mTime += ImGui::GetIO().DeltaTime;
	}

	// Dump Texture
	{
		if (gCPUContext.mDumpTextureRef != nullptr && gCPUContext.mDumpTextureRef->mResource != nullptr)
		{
			DirectX::ScratchImage image;
			DirectX::CaptureTexture(gCommandQueue, gCPUContext.mDumpTextureRef->mResource.Get(), false, image, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON);

			std::filesystem::path path = gCreateDumpFolder();
			path += gCPUContext.mDumpTextureRef->mName;
			path += ".dds";
			DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, path.c_str());

			gCPUContext.mDumpTextureRef = nullptr;
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

	// Check SM 6.7
	D3D12_FEATURE_DATA_SHADER_MODEL shader_model = { D3D_SHADER_MODEL_6_7 };
	if (FAILED(gDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
		|| (shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_7))
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

	// CommandQueue
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		gValidate(gDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&gCommandQueue)));
		gCommandQueue->SetName(L"gCommandQueue");
		gCommandQueue->GetTimestampFrequency(&gTiming.mTimestampFrequency);
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

		gSwapChain->SetMaximumFrameLatency(kFrameInFlightCount);
		gSwapChainWaitableObject = gSwapChain->GetFrameLatencyWaitableObject();
	}

	return true;
}

static void sCleanupDeviceD3D()
{
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
		{
			// INI
			auto SetPrivateProfileInt = [](LPCWSTR lpAppName, LPCWSTR lpKeyName, INT nValue, LPCWSTR lpFileName)
			{
				wchar_t buffer[16];
				_itow_s(nValue, buffer, std::size(buffer), 10);
				return WritePrivateProfileString(lpAppName, lpKeyName, buffer, lpFileName) != 0;
			};
			RECT rect_for_ini;
			gAssert(::GetWindowRect(hWnd, &rect_for_ini));
			SetPrivateProfileInt(L"Main", L"Window_X", rect_for_ini.left, kINIPathW);
			SetPrivateProfileInt(L"Main", L"Window_Y", rect_for_ini.top, kINIPathW);
			WritePrivateProfileString(NULL, NULL, NULL, kINIPathW); // Flush
		}		
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
