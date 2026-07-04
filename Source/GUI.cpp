#include "GUI.h"
#include "Common.h"
#include "Renderer.h"
#include "Scene.h"
#include "Atmosphere.h"
#include "Cloud.h"

void gPrepareImGui()
{
	using namespace ImGui;

	std::string stat = std::format("Frame {} | Time {:.3f} s | Average {:.3f} ms (FPS {:.1f}) | {}x{}###DXRPlayground",
		gConstants.mCurrentFrameIndex,
		gConstants.mTime,
		1000.0f / GetIO().Framerate,
		GetIO().Framerate,
		gRenderer.mScreenSize.x,
		gRenderer.mScreenSize.y);
	if (Begin(stat.c_str()))
	{
		PushItemWidth(240.0f);

		{
			if (Button("Reload Shader (F5)"))
				gRenderer.mReloadShader = true;

			SameLine();

			if (Button("Reload Camera (F6)"))
				gLoadCamera();

			SameLine();

			if (Button("Copy Mitsuba Camera"))
			{
				glm::mat4x4 camera_transform = gConstants.mCameraTransform;
				SetClipboardText(gToString(camera_transform).c_str());
			}
		}
		{
			if (Button("Reload Scene (F4)"))
				gRenderer.mReloadScene = true;

			SameLine();

			if (Button("Open Scene Folder"))
				gOpenSceneDirectoryInExplorer(ScenePreset::sCurrent().mPath);
		}
		{
			if (Button("Dump Luminance (F9)"))
				gDumpLuminance();

			SameLine();

			if (Button("Dump Shader"))
			{
				gRenderer.mDumpRayQuery = true;
				gRenderer.mReloadShader = true;
			}

			SameLine();

			if (Button("Open Dump Folder"))
				gOpenDumpDirectoryInExplorer();
		}

		if (CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
		{
			InputInt2("Coords", (int*)&gConstants.mPixelDebugCoord);
			SliderInt("Light Index", &gConstants.mPixelDebugLightIndex, 0, (int)gScene.GetSceneContent().mLights.size() - 1);
			InputFloat3("Screen Color", &gRenderer.mRuntime.mInspectDataBuffer.GetReadback<InspectData>(gGetFrameContextIndex())[0].mScreenColor.x, "%.8f", ImGuiInputTextFlags_ReadOnly);
			SliderInt("Recursion", &gConstants.mDebugRecursion, 0, gConstants.mRecursionDepthCountMax);

			if (TreeNodeEx("Visualize", ImGuiTreeNodeFlags_None))
			{
				for (int i = 0; i < static_cast<int>(VisualizeMode::Count); i++)
				{
					const auto& name = nameof::nameof_enum(static_cast<VisualizeMode>(i));
					if (name.starts_with('_'))
					{
						NewLine();
						continue;
					}

					if (i != 0)
						SameLine();

					RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mVisualizeMode), i);
				}
				TreePop();
			}

			if (TreeNodeEx("ShaderPrint", ImGuiTreeNodeFlags_None))
			{
				std::span<uint> ShaderPrint = gRenderer.mRuntime.mShaderPrintBuffer.GetReadback<uint>(gGetFrameContextIndex());

				uint uint_count = ShaderPrint[0];
				gAssert(uint_count <= ShaderPrint.size());
				SameLine();
				Text("(UInt Count = %d)", uint_count);

				std::stringstream ss;
				uint uint_index = 1;
				auto process_next = [&]()
					{
						if (uint_index >= uint_count) { return false; }

						uint entry_header = ShaderPrint[uint_index++];
						ShaderPrintEntryType type = (ShaderPrintEntryType)(entry_header & 0xffff);
						uint option = entry_header >> 16;

						switch (type)
						{
						case ShaderPrintEntryType::Float1:
						{
							ss << asfloat(ShaderPrint[uint_index++]);
							if (option & 0x1) { ss << "\n"; }
							break;
						}
						case ShaderPrintEntryType::Float2:
						{
							ss << asfloat(ShaderPrint[uint_index++]) << ", " << asfloat(ShaderPrint[uint_index++]);
							if (option & 0x1) { ss << "\n"; }
							break;
						}
						case ShaderPrintEntryType::Float3:
						{
							ss << asfloat(ShaderPrint[uint_index++]) << ", " << asfloat(ShaderPrint[uint_index++]) << ", " << asfloat(ShaderPrint[uint_index++]);
							if (option & 0x1) { ss << "\n"; }
							break;
						}
						case ShaderPrintEntryType::Float4:
						{
							ss << asfloat(ShaderPrint[uint_index++]) << ", " << asfloat(ShaderPrint[uint_index++]) << ", " << asfloat(ShaderPrint[uint_index++]) << ", " << asfloat(ShaderPrint[uint_index++]);
							if (option & 0x1) { ss << "\n"; }
							break;
						}
						case ShaderPrintEntryType::UInt1:
						{
							ss << (ShaderPrint[uint_index++]);
							if (option & 0x1) { ss << "\n"; }
							break;
						}
						case ShaderPrintEntryType::UInt2:
						{
							ss << (ShaderPrint[uint_index++]) << ", " << (ShaderPrint[uint_index++]);
							if (option & 0x1) { ss << "\n"; }
							break;
						}
						case ShaderPrintEntryType::UInt3:
						{
							ss << (ShaderPrint[uint_index++]) << ", " << (ShaderPrint[uint_index++]) << ", " << (ShaderPrint[uint_index++]);
							if (option & 0x1) { ss << "\n"; }
							break;
						}
						case ShaderPrintEntryType::UInt4:
						{
							ss << (ShaderPrint[uint_index++]) << ", " << (ShaderPrint[uint_index++]) << ", " << (ShaderPrint[uint_index++]) << ", " << (ShaderPrint[uint_index++]);
							if (option & 0x1) { ss << "\n"; }
							break;
						}
						case ShaderPrintEntryType::String:
						{
							uint string_byte_count = option;
							uint string_uint_count = (string_byte_count + 3) / 4;
							std::string_view string((const char*)(&ShaderPrint[uint_index]), string_byte_count);
							ss << string;
							uint_index += string_uint_count;
							break;
						}
						}

						return true;
					};

				while (process_next()) {};
				auto str = ss.str();
				ImGui::InputTextMultiline("ShaderPrint", str.data(), str.length(), ImVec2(0, 0), ImGuiInputTextFlags_ReadOnly);

				TreePop();
			}
		}

		if (CollapsingHeader("Sampling", ImGuiTreeNodeFlags_DefaultOpen))
		{
			Text("Offset Mode");
			for (int i = 0; i < static_cast<int>(OffsetMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<OffsetMode>(i));
				SameLine();
				RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mOffsetMode), i);
			}

			Text("Sample Mode");
			for (int i = 0; i < static_cast<int>(SampleMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<SampleMode>(i));
				SameLine();
				RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mSampleMode), i);
			}

			if (TreeNodeEx("ReSTIR", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (SliderInt("Initial Sample Count", reinterpret_cast<int*>(&gConstants.mReSTIR.mInitialSampleCount), 1, 8))
					gRenderer.mFrameResetRequested = true;

				if (Checkbox("Temporal", (bool*)& gConstants.mReSTIR.mTemporalReuseCount))
					gRenderer.mFrameResetRequested = true;

				if (SliderInt("Spatial Sample Count", reinterpret_cast<int*>(&gConstants.mReSTIR.mSpatialReuseCount), 1, 8))
					gRenderer.mFrameResetRequested = true;

				TreePop();
			}
		}

		if (CollapsingHeader("Accumulation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (int i = 0; i < static_cast<int>(AccumulationMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<AccumulationMode>(i));
				RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mAccumulationMode), i);
				SameLine();
			}
			NewLine();

			if (Checkbox("Frame Count Unlimited", &gRenderer.mFrameUnlimited))
				gRenderer.mFrameResetRequested = true;
			SameLine();
			Checkbox("Paused", &gRenderer.mFramePaused);
			SameLine();
			Checkbox("Vsync", &gDisplaySettings.mVsync);

			if (!gRenderer.mFrameUnlimited)
			{
				if (SliderInt("Frame Count", reinterpret_cast<int*>(&gRenderer.mFrameCount), 1, 512))
					gRenderer.mFrameResetRequested = true;

				BeginDisabled();
				SliderInt("Frame Index", &gConstants.mCurrentFrameIndex, 0, gRenderer.mFrameCount - 1);
				EndDisabled();
			}

			SliderInt("Recursion Depth Max", reinterpret_cast<int*>(&gConstants.mRecursionDepthCountMax), 1, 64);
			SliderInt("Russian Roulette Depth", reinterpret_cast<int*>(&gConstants.mRussianRouletteDepth), 1, gConstants.mRecursionDepthCountMax);
		}

		if (CollapsingHeader("Camera"))
		{
			InputFloat3("Position", (float*)&gConstants.mCameraTransform[3]);
			InputFloat3("Direction", (float*)&gConstants.mCameraTransform[2], "%.3f", ImGuiInputTextFlags_ReadOnly);
			SliderFloat("Horz Fov", (float*)&gCameraSettings.mHorizontalFovDegree, 30.0f, 160.0f);

			PushID("Aperture");
			{
				SliderFloat("Aperture", &gCameraSettings.mExposureControl.mAperture, 1.0f, 22.0f);
				SameLine();
				if (Button("<")) { gCameraSettings.mExposureControl.mAperture /= glm::sqrt(2.0f); }
				SameLine();
				if (Button(">")) { gCameraSettings.mExposureControl.mAperture *= glm::sqrt(2.0f); }
			}
			PopID();
			PushID("Shutter Speed");
			{
				std::string format = "%.3f";
				if (gCameraSettings.mExposureControl.mInvShutterSpeed < 1.0f)
					format += std::format(" ({:.1f}sec)", 1.0f / gCameraSettings.mExposureControl.mInvShutterSpeed);
				SliderFloat("Shutter Speed (1/sec)", &gCameraSettings.mExposureControl.mInvShutterSpeed, 1.0f, 500.0f, format.c_str());
				SameLine();
				if (Button("<")) { gCameraSettings.mExposureControl.mInvShutterSpeed /= 2.0f; }
				SameLine();
				if (Button(">")) { gCameraSettings.mExposureControl.mInvShutterSpeed *= 2.0f; }
			}
			PopID();
			PushID("ISO");
			{
				SliderFloat("ISO", &gCameraSettings.mExposureControl.mSensitivity, 100.0f, 3200.0f);
				SameLine();
				if (Button("<")) { gCameraSettings.mExposureControl.mSensitivity /= 2.0f; }
				SameLine();
				if (Button(">")) { gCameraSettings.mExposureControl.mSensitivity *= 2.0f; }
			}
			PopID();

			if (SmallButton("Reset Exposure"))
				gCameraSettings.ResetExposure();
			SameLine();
			Text("EV100 = %.2f", gConstants.mEV100);

			Text("ToneMappingMode");
			for (int i = 0; i < static_cast<int>(ToneMappingMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<ToneMappingMode>(i));
				SameLine();
				RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mToneMappingMode), i);
			}
		}

		if (CollapsingHeader("Scene"))
		{
			for (int i = 0; i < ScenePreset::sCount(); i++)
			{
				if (!ScenePreset::sPresets[i].mName.empty())
					RadioButton(ScenePreset::sPresets[i].mName.data(), &ScenePreset::sCurrentIndex, i);
			}

			SliderFloat("Emission Boost", &gConstants.mEmissionBoost, 1E-16f, 1E16F);
			SliderFloat("Density Boost", &gConstants.mDensityBoost, 1E-16f, 1E16F);
		}

		if (CollapsingHeader("Atmosphere"))
		{
			gAtmosphere.ImGuiShowMenus();
		}

		//if (CollapsingHeader("Cloud"))
		//{
		//	gCloud.ImGuiShowMenus();
		//}

		if (CollapsingHeader("Spatial Cache"))
		{
			Checkbox("Active", (bool*)&gConstants.mSpatialCache.mFrameActive);

			if (Button("Active Once"))
			{
				gConstants.mSpatialCache.mFrameActive = true;
				gRenderer.mSpatialCacheActiveOnce = true;
			}

			if (Button("Reset"))
				gRenderer.mSpatialCacheResetRequested = true;
		}

		if (CollapsingHeader("BRDF Explorer"))
		{
			Texture1(gRenderer.mRuntime.mBRDFSliceTexture);

			ColorEdit3("BaseColor", &gConstants.mBRDFExplorer.mBaseColor.x);
			SliderFloat("Metallic", &gConstants.mBRDFExplorer.mMetallic, 0.0f, 1.0f);
			SliderFloat("Subsurface", &gConstants.mBRDFExplorer.mSubsurface, 0.0f, 1.0f);
			SliderFloat("Specular", &gConstants.mBRDFExplorer.mSpecular, 0.0f, 1.0f);
			SliderFloat("Roughness", &gConstants.mBRDFExplorer.mRoughness, 0.0f, 1.0f);
			SliderFloat("SpecularTint", &gConstants.mBRDFExplorer.mSpecularTint, 0.0f, 1.0f);
			SliderFloat("Anisotropic", &gConstants.mBRDFExplorer.mAnisotropic, 0.0f, 1.0f);
			SliderFloat("Sheen", &gConstants.mBRDFExplorer.mSheen, 0.0f, 1.0f);
			SliderFloat("SheenTint", &gConstants.mBRDFExplorer.mSheenTint, 0.0f, 1.0f);
			SliderFloat("Clearcoat", &gConstants.mBRDFExplorer.mClearcoat, 0.0f, 1.0f);
			SliderFloat("ClearcoatGloss", &gConstants.mBRDFExplorer.mClearcoatGloss, 0.0f, 1.0f);

			Separator();

			SliderAngle("PhiD", &gConstants.mBRDFExplorer.mPhiD, 0.0f, 180.0f);
			SliderFloat("Gamma", &gConstants.mBRDFExplorer.mGamma, 1.0f, 2.2f);

			Checkbox("Override Instance", (bool*)& gConstants.mBRDFExplorer.mOverrideInstance);

			if (Button("Reset"))
			{
				gConstants.mBRDFExplorer = {};
			}
		}

		if (CollapsingHeader("NVAPI", ImGuiTreeNodeFlags_None))
		{
			if (Button("Reload Scene"))
				gRenderer.mReloadScene = true;

			if (TreeNodeEx("LSS", ImGuiTreeNodeFlags_None))
			{
				bool endcap_chained = gNVAPI.mEndcapMode == NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED;
				if (Checkbox("Endcap Chained", &endcap_chained))
				{
					gNVAPI.mEndcapMode = endcap_chained ? NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED : NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_NONE;
					gRenderer.mReloadScene = true;
				}

				TreePop();
			}

			if (TreeNodeEx("Sphere Surface", ImGuiTreeNodeFlags_None))
			{
				InputInt("Fill Count X", &gNVAPI.mSphereSurfaceFillCountX);
				InputFloat("Radius", &gNVAPI.mSphereSurfaceFillRadius);
				Checkbox("Random", &gNVAPI.mSphereSurfaceRandom);

				TreePop();
			}

			if (TreeNodeEx("LSS (Wireframe)", ImGuiTreeNodeFlags_None))
			{
				if (Checkbox("Enabled", &gNVAPI.mLSSWireframeEnabled))
					gRenderer.mReloadScene = true;

				bool endcap_chained = gNVAPI.mLSSWireframeEndcapMode == NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED;
				if (Checkbox("Endcap Chained", &endcap_chained))
				{
					gNVAPI.mLSSWireframeEndcapMode = endcap_chained ? NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_CHAINED : NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE_NONE;
					gRenderer.mReloadScene = true;
				}

				SliderFloat("Radius", &gNVAPI.mLSSWireframeRadius, 0.001f, 0.1f);

				TreePop();
			}

			if (TreeNodeEx("Cluster", ImGuiTreeNodeFlags_None))
			{
				if (Checkbox("Enabled", &gNVAPI.mClusterEnabled))
					gRenderer.mReloadScene = true;

				TreePop();
			}

			if (TreeNodeEx("Caps", ImGuiTreeNodeFlags_None))
			{
				BeginDisabled();

				Checkbox("Micromap", &gNVAPI.mMicromapSupported);
				Checkbox("Clusters", &gNVAPI.mClusterSupported);
				Checkbox("LinearSweptSpheres", &gNVAPI.mLinearSweptSpheresSupported);
				Checkbox("Spheres", &gNVAPI.mSpheresSupported);
				Checkbox("ShaderExecutionReordering", &gNVAPI.mShaderExecutionReorderingSupported);

				EndDisabled();

				TreePop();
			}
		}

		if (CollapsingHeader("Sequence", ImGuiTreeNodeFlags_None))
		{
			Checkbox("Enabled", (bool*)&gConstants.mSequenceEnabled);
			SameLine();
			Checkbox("Camera", &gRenderer.mSequenceCameraEnabled);
			SameLine();
			Checkbox("Vsync", &gDisplaySettings.mVsync);

			if (Button("Dump PNG"))
				gRenderer.mSequenceDumpPNG = true;

			SliderInt("Sequence Frame Count", &gConstants.mSequenceFrameCount, 1, 600);
			SliderInt("Sequence Frame Index", &gConstants.mSequenceFrameIndex, 0, gConstants.mSequenceFrameCount - 1);
			BeginDisabled();
			SliderInt("Accumulation Frame Index", &gConstants.mCurrentFrameIndex, 0, gRenderer.mFrameCount - 1);
			EndDisabled();

			if (Button("Record"))
			{
				gConstants.mSequenceEnabled = 1;
				gConstants.mCurrentFrameIndex = 0;
				gConstants.mSequenceFrameIndex = 0;

				gRenderer.mSequenceFrameRecording = 0;
			}
		}

		if (CollapsingHeader("Display"))
		{
			Checkbox("Vsync", &gDisplaySettings.mVsync);

			if (Button("1280 x 720")) { gRenderer.mScreenSizeRequested = { 1280, 720 }; }
			if (Button("1920 x 1080")) { gRenderer.mScreenSizeRequested = { 1920, 1080 }; }
			if (Button("2560 x 1440")) { gRenderer.mScreenSizeRequested = { 2560, 1440 }; }
		}

		if (CollapsingHeader("Config"))
		{
			if (Checkbox("Shader Debug", &gConfigs.mShaderDebug))
				gRenderer.mReloadShader = true;

			if (Checkbox("Use Texture", &gConfigs.mUseTexture))
				gRenderer.mReloadShader = true;

			if (Checkbox("Test Hit Shader", &gConfigs.mTestHitShader))
				gRenderer.mReloadShader = true;

			if (Checkbox("Test Slang Shader", &gConfigs.mTestSlangShader))
				gRenderer.mReloadShader = true;

			if (Checkbox("NanoVDB Generate Texture (in Scene Textures)", &gConfigs.mNanoVDBGenerateTexture))
				gRenderer.mReloadScene = true;

			if (Checkbox("NanoVDB Use Texture (Require Generate)", &gConfigs.mNanoVDBUseTexture))
				gRenderer.mReloadShader = true;
		}

		// Floating items
		{
			gRenderer.ImGuiShowTextures();
			gScene.ImGuiShowTextures();
			gAtmosphere.ImGuiShowTextures();
			gCloud.ImGuiShowTextures();

			if (Begin("Inspector"))
			{
				InspectData& inspect_data = gRenderer.mRuntime.mInspectDataBuffer.GetReadback<InspectData>(gGetFrameContextIndex())[0];

				InputFloat3("Debug", &inspect_data.mScreenDebug.x, "%.8f", ImGuiInputTextFlags_ReadOnly);

				for (int i = 0; i < static_cast<int>(InspectMode::Count); i++)
				{
					const auto& name = nameof::nameof_enum(static_cast<InspectMode>(i));
					if (name.starts_with('_'))
					{
						NewLine();
						continue;
					}

					if (i != 0)
						SameLine();

					RadioButton(name.data(), reinterpret_cast<int*>(&gConstants.mInspectMode), i);
				}

				for (int i = 0; i < InspectData::kPathLength; i++)
				{
					float4& value = gRenderer.mRuntime.mInspectDataBuffer.GetReadback<InspectData>(gGetFrameContextIndex())[0].mValue[i];
					PushStyleColor(ImGuiCol_Text, GetColorU32(ImGuiCol_Text, value.w == 0.0f ? 0.2f : 1.0f));
					InputFloat4(std::to_string(i).c_str(), &value.x, "%.8f", ImGuiInputTextFlags_ReadOnly);
					PopStyleColor();
				}
			}
			End();

			if (Begin("Instances"))
			{
				const char* columns[] =
				{
					"Index",
					"Name",
					"Position",
					"Scale",
					"Material",
					"BSDF",
					"Albedo",
					"Reflectance",
					"Transmittance",
					"Eta",
					"K",
					"Medium",
					"MediumAlbedo",
					"MediumSigmaT",
					"MediumPhase",
					"Emission",
					"RoughnessAlpha",
					"Opacity",
					"VertexCount",
					"PrimitiveCount",
					"ScratchMB",
					"ResultMB",
				};
				int column_count = (int)std::size(columns);

				if (BeginTable("Table", column_count, ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
				{
					TableSetupScrollFreeze(0, 1); // Make top row always visible

					TableNextRow(ImGuiTableRowFlags_Headers);
					for (int i = 0; i < column_count; i++)
					{
						TableSetColumnIndex(i);
						Text(columns[i]);

						if (IsItemHovered())
						{
							SetTooltip(columns[i]);
						}
					}

					for (int row = 0; row < gScene.GetInstanceCount(); row++)
					{
						TableNextRow();
						PushID(row);

						const InstanceInfo& instance_info = gScene.GetInstanceInfo(row);
						const InstanceData& instance_data = gScene.GetInstanceData(row);

						int column_index = 0;

						TableSetColumnIndex(column_index++);
						if (Selectable(std::to_string(row).c_str(), row == gConstants.mDebugInstanceID, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnNav))
							gConstants.mDebugInstanceID = row;

						if (row == gRenderer.mRuntime.mInspectDataBuffer.GetReadback<InspectData>(gGetFrameContextIndex())[0].mPixelInstanceID)
							TableSetBgColor(ImGuiTableBgTarget_RowBg1, GetColorU32(ImVec4(0.8f, 0.2f, 0.2f, 0.8f)));

						TableSetColumnIndex(column_index++);
						Text("%s", instance_info.mName.c_str());

						TableSetColumnIndex(column_index++);
						std::string position = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mTransform[3][0], instance_data.mTransform[3][1], instance_data.mTransform[3][2]);
						position = glm::dot(glm::vec3(instance_data.mTransform[3]), glm::vec3(instance_data.mTransform[3])) != 0.0f ? position : "";
						Text(position.c_str());

						TableSetColumnIndex(column_index++);
						std::string scale = std::format("{:.2f} {:.2f} {:.2f} ", instance_info.mDecomposedScale[0], instance_info.mDecomposedScale[1], instance_info.mDecomposedScale[2]);
						scale = instance_info.mDecomposedScale != glm::vec3(1.0f) ? scale : "";
						Text(scale.c_str());

						TableSetColumnIndex(column_index++);
						Text("%s", instance_info.mMaterial.mMaterialName.c_str());

						TableSetColumnIndex(column_index++);
						Text("%s%s", NAMEOF_ENUM(instance_data.mBSDF).data(), instance_data.mFlags.mTwoSided ? " (TwoSided)" : "");

						TableSetColumnIndex(column_index++);
						std::string albedo = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mAlbedo.x, instance_data.mAlbedo.y, instance_data.mAlbedo.z);
						albedo = instance_data.mAlbedo != InstanceData().mAlbedo ? albedo : "";
						albedo += instance_info.mMaterial.mAlbedoTexture.empty() ? "" : (instance_info.mMaterial.mAlbedoTexture.filename().string() + " (" + std::to_string(instance_data.mAlbedoTexture.mTextureIndex) + ", " + std::to_string(instance_data.mAlbedoTexture.mSamplerIndex) + ")");
						Text(albedo.c_str());

						TableSetColumnIndex(column_index++);
						std::string reflectance = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mReflectance.x, instance_data.mReflectance.y, instance_data.mReflectance.z);
						reflectance = instance_data.mReflectance != InstanceData().mReflectance ? reflectance : "";
						reflectance += instance_info.mMaterial.mReflectanceTexture.empty() ? "" : (instance_info.mMaterial.mReflectanceTexture.filename().string() + " (" + std::to_string(instance_data.mReflectanceTexture.mTextureIndex) + ", " + std::to_string(instance_data.mReflectanceTexture.mSamplerIndex) + ")");
						Text(reflectance.c_str());

						TableSetColumnIndex(column_index++);
						std::string transmittance = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mSpecularTransmittance.x, instance_data.mSpecularTransmittance.y, instance_data.mSpecularTransmittance.z);
						transmittance = instance_data.mSpecularTransmittance != InstanceData().mSpecularTransmittance ? transmittance : "";
						Text(transmittance.c_str());

						TableSetColumnIndex(column_index++);
						std::string eta = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mEta.x, instance_data.mEta.y, instance_data.mEta.z);
						eta = instance_data.mEta != InstanceData().mEta ? eta : "";
						Text(eta.c_str());

						TableSetColumnIndex(column_index++);
						std::string k = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mK.x, instance_data.mK.y, instance_data.mK.z);
						k = instance_data.mK != InstanceData().mK ? k : "";
						Text(k.c_str());

						TableSetColumnIndex(column_index++);
						bool medium = instance_data.mMedium != 0;
						Checkbox("##Medium", &medium);

						TableSetColumnIndex(column_index++);
						std::string medium_albedo = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mMediumAlbedo.x, instance_data.mMediumAlbedo.y, instance_data.mMediumAlbedo.z);
						medium_albedo = instance_data.mMediumAlbedo != InstanceData().mMediumAlbedo ? medium_albedo : "";
						Text(medium_albedo.c_str());

						TableSetColumnIndex(column_index++);
						std::string medium_sigma_t = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mMediumSigmaT.x, instance_data.mMediumSigmaT.y, instance_data.mMediumSigmaT.z);
						medium_sigma_t = instance_data.mMediumSigmaT != InstanceData().mMediumSigmaT ? medium_sigma_t : "";
						Text(medium_sigma_t.c_str());

						TableSetColumnIndex(column_index++);
						std::string medium_phase = std::format("{:.2f} ", instance_data.mMediumPhase);
						medium_phase = instance_data.mMediumPhase != InstanceData().mMediumPhase ? medium_phase : "";
						Text(medium_phase.c_str());

						TableSetColumnIndex(column_index++);
						std::string emission = std::format("{:.2f} {:.2f} {:.2f} ", instance_data.mEmission.x, instance_data.mEmission.y, instance_data.mEmission.z);
						emission = instance_data.mEmission != InstanceData().mEmission ? emission : "";
						emission += instance_info.mMaterial.mEmissionTexture.empty() ? "" : (instance_info.mMaterial.mEmissionTexture.filename().string() + " (" + std::to_string(instance_data.mEmissionTexture.mTextureIndex) + ", " + std::to_string(instance_data.mEmissionTexture.mSamplerIndex) + ")");
						Text(emission.c_str());

						TableSetColumnIndex(column_index++);
						std::string roughness_alpha = std::format("{:.2f} ", instance_data.mRoughnessAlpha);
						roughness_alpha = instance_data.mRoughnessAlpha != InstanceData().mRoughnessAlpha ? roughness_alpha : "";
						Text(roughness_alpha.c_str());

						TableSetColumnIndex(column_index++);
						std::string opacity = std::format("{:.2f} ", instance_data.mOpacity);
						Text(opacity.c_str());

						TableSetColumnIndex(column_index++);
						std::string vertex_count = std::format("{} ", instance_data.mVertexCount);
						Text(vertex_count.c_str());

						TableSetColumnIndex(column_index++);
						std::string index_count = std::format("{} ", instance_data.mIndexCount / kIndexCountPerTriangle);
						Text(index_count.c_str());

						TableSetColumnIndex(column_index++);
						std::string scratch_data_size_in_mb = std::format("{:.2f} ", instance_info.mStats.mScratchDataSizeInBytes / 1024.0f / 1024.0f);
						Text(scratch_data_size_in_mb.c_str());

						TableSetColumnIndex(column_index++);
						std::string bvh_data_size_in_mb = std::format("{:.2f} ", instance_info.mStats.mResultDataSizeInBytes / 1024.0f / 1024.0f);
						Text(bvh_data_size_in_mb.c_str());

						PopID();
						gAssert(column_index == column_count);
					}
					EndTable();
				}

				gConstants.mDebugInstanceID = glm::clamp(gConstants.mDebugInstanceID, -1, gScene.GetInstanceCount() - 1);
			}
			End();

			if (Begin("Lights"))
			{
				static const int kColumnCount = 6;
				if (BeginTable("Table", kColumnCount, ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders))
				{
					TableSetupScrollFreeze(0, 1); // Make top row always visible

					{
						int column_index = 0;
						TableNextRow(ImGuiTableRowFlags_Headers);
						TableSetColumnIndex(column_index++); Text("Index");
						TableSetColumnIndex(column_index++); Text("Name");
						TableSetColumnIndex(column_index++); Text("Position");
						TableSetColumnIndex(column_index++); Text("HalfExtends");
						TableSetColumnIndex(column_index++); Text("Type");
						TableSetColumnIndex(column_index++); Text("Emission");
						gAssert(column_index == kColumnCount);
					}

					for (int row = 0; row < gScene.GetLightCount(); row++)
					{
						TableNextRow();

						const Light& light = gScene.GetLight(row);
						const InstanceInfo& instance_info = gScene.GetInstanceInfo(light.mInstanceID);

						int column_index = 0;

						TableSetColumnIndex(column_index++);
						if (Selectable(std::to_string(row).c_str(), row == gConstants.mDebugLightIndex, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnNav))
							gConstants.mDebugLightIndex = row;

						if (static_cast<int>(light.mInstanceID) == gRenderer.mRuntime.mInspectDataBuffer.GetReadback<InspectData>(gGetFrameContextIndex())[0].mPixelInstanceID)
							TableSetBgColor(ImGuiTableBgTarget_RowBg1, GetColorU32(ImVec4(0.8f, 0.2f, 0.2f, 0.8f)));

						TableSetColumnIndex(column_index++);
						Text("%s", instance_info.mName.c_str());

						TableSetColumnIndex(column_index++);
						std::string position = std::format("{:.2f} {:.2f} {:.2f}", light.mPosition.x, light.mPosition.y, light.mPosition.z);
						Text(position.c_str());

						TableSetColumnIndex(column_index++);
						std::string half_extends = std::format("{:.2f} {:.2f}", light.mHalfExtends.x, light.mHalfExtends.y);
						Text(half_extends.c_str());

						TableSetColumnIndex(column_index++);
						Text("%s", NAMEOF_ENUM(light.mType).data());

						TableSetColumnIndex(column_index++);
						std::string emission = std::format("{:.2f} {:.2f} {:.2f}", light.mEmission.x, light.mEmission.y, light.mEmission.z);
						emission = glm::dot(light.mEmission, light.mEmission) != 0.0f ? emission : "";
						Text(emission.c_str());

						gAssert(column_index == kColumnCount);
					}
					EndTable();
				}
			}
			End();

			if (Begin("Stats"))
			{
				if (TreeNodeEx("InstructionCount", ImGuiTreeNodeFlags_DefaultOpen))
				{
					InputInt("Instruction Count", &gStats.mInstructionCount.mRayQuery, 0, 0, ImGuiInputTextFlags_ReadOnly);

					TreePop();
				}

				if (TreeNodeEx("GPU Timing (MS)", ImGuiTreeNodeFlags_DefaultOpen))
				{
					InputFloat("Upload",			&gStats.mGPUTimingMS.mUpload,			0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("Renderer",			&gStats.mGPUTimingMS.mRenderer,			0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("Scene",				&gStats.mGPUTimingMS.mScene,			0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("Atmosphere",		&gStats.mGPUTimingMS.mAtmosphere,		0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("Cloud",				&gStats.mGPUTimingMS.mCloud,			0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("TextureGenerator",	&gStats.mGPUTimingMS.mTextureGenerator, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("BRDFSlice",			&gStats.mGPUTimingMS.mBRDFSlice,		0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("Clear",				&gStats.mGPUTimingMS.mClear,			0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("Depths",			&gStats.mGPUTimingMS.mDepths,			0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("PrepareLights",		&gStats.mGPUTimingMS.mPrepareLights,	0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("RayQuery",			&gStats.mGPUTimingMS.mRayQuery,			0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("Composite",			&gStats.mGPUTimingMS.mComposite,		0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);
					InputFloat("ImGui",				&gStats.mGPUTimingMS.mImGui,			0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

					static ScrollingBuffer sRayQueryBuffer;
					sRayQueryBuffer.AddPoint(gConstants.mTime, gStats.mGPUTimingMS.mRayQuery);

					float time = gConstants.mTime;
					if (ImPlot::BeginPlot("Time", ImVec2(-1, 400)))
					{
						ImPlot::SetupAxisLimits(ImAxis_X1, time - 10.0f, time, ImGuiCond_Always);
						ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 5.0f);

						ImPlot::PlotLine("RayQuery", &sRayQueryBuffer.Data[0].x, &sRayQueryBuffer.Data[0].y, sRayQueryBuffer.Data.size(), 0, sRayQueryBuffer.Offset, 2 * sizeof(float));

						ImPlot::EndPlot();
					}

					TreePop();
				}

				if (TreeNodeEx("CPU Timing (MS)", ImGuiTreeNodeFlags_DefaultOpen))
				{
					InputFloat("Startup", &gStats.mCPUTimingMS.mStartup, 0, 0, "%.3f", ImGuiInputTextFlags_ReadOnly);

					TreePop();
				}
			}
			End();
		}

		PopItemWidth();
	}
	End();
}