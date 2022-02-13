#include "Atmosphere.h"
#include "ImGui/imgui_impl_helper.h"

void Atmosphere::Update()
{
	AtmosphereConstants* constants				= static_cast<AtmosphereConstants*>(gAtmosphere.mResource.mConstantUploadBufferPointer);

	auto inv_m_to_inv_km						= [](glm::f64 inInvM) { return static_cast<float>(inInvM * 1000.0); };

	constants->mBottomRadius					= static_cast<float>(gAtmosphere.mProfile.BottomRadius());
	constants->mTopRadius						= static_cast<float>(gAtmosphere.mProfile.TopRadius());
	constants->mSceneScale						= gAtmosphere.mProfile.mSceneInKilometer ? 1.0f : 0.001f;

	constants->mMode							= gAtmosphere.mProfile.mMode;
	constants->mMuSEncodingMode					= gAtmosphere.mProfile.mMuSEncodingMode;
	constants->mSliceCount						= gAtmosphere.mResource.mSliceCount;

	constants->mConstantColor					= gAtmosphere.mProfile.mConstantColor;

	constants->mSolarIrradiance					= gAtmosphere.mProfile.mSolarIrradiance;
	constants->mSunAngularRadius				= static_cast<float>(gAtmosphere.mProfile.kSunAngularRadius);

	constants->mAerialPerspective				= (gAtmosphere.mProfile.mMode != AtmosphereMode::RaymarchAtmosphereOnly && gAtmosphere.mProfile.mAerialPerspective) ? 1.0f : 0.0f;
	constants->mGroundAlbedo					= gAtmosphere.mProfile.mGroundAlbedo;
	constants->mRuntimeGroundAlbedo				= gAtmosphere.mProfile.mRuntimeGroundAlbedo;

	// Density Profile
	{
		// Rayleigh
		{
			// Scattering Coefficient
			constants->mRayleighScattering		= gAtmosphere.mProfile.mEnableRayleigh ? gAtmosphere.mProfile.mRayleighScatteringCoefficient : glm::dvec3(1e-9);

			// Extinction Coefficient
			constants->mRayleighExtinction		= gAtmosphere.mProfile.mEnableRayleigh ? gAtmosphere.mProfile.mRayleighScatteringCoefficient : glm::dvec3(1e-9);

			// Density
			constants->mRayleighDensity			= gAtmosphere.mProfile.mRayleighDensityProfile;
		}

		// Mie
		{
			// Scattering Coefficient
			constants->mMieScattering			= gAtmosphere.mProfile.mEnableMie ? gAtmosphere.mProfile.mMieScatteringCoefficient : glm::dvec3(1e-9);

			// Extinction Coefficient
			constants->mMieExtinction			= gAtmosphere.mProfile.mEnableMie ? gAtmosphere.mProfile.mMieExtinctionCoefficient : glm::dvec3(1e-9);

			// Phase function
			constants->mMiePhaseFunctionG		= static_cast<float>(gAtmosphere.mProfile.mMiePhaseFunctionG);

			// Density
			constants->mMieDensity				= gAtmosphere.mProfile.mMieDensityProfile;
		}

		// Ozone
		{
			// Extinction Coefficient
			constants->mOzoneExtinction			= gAtmosphere.mProfile.mEnableOzone ? gAtmosphere.mProfile.mOZoneAbsorptionCoefficient : glm::dvec3();

			// Density
			constants->mOzoneDensity				= gAtmosphere.mProfile.mOzoneDensityProfile;
		}
	}
}

void Atmosphere::Load()
{
	for (auto&& texture : gAtmosphere.mResource.mTextures)
		texture.Load();

	for (auto&& texture : gAtmosphere.mResource.mValidation.mTextures)
		texture.Load();
}

void Atmosphere::ComputeTransmittance()
{
	gAtmosphere.mResource.mComputeTransmittanceShader.SetupCompute();
	gCommandList->Dispatch(gAtmosphere.mResource.mTransmittanceTexture.mWidth / 8, gAtmosphere.mResource.mTransmittanceTexture.mHeight / 8, gAtmosphere.mResource.mTransmittanceTexture.mDepth);
}

void Atmosphere::ComputeDirectIrradiance()
{
	gAtmosphere.mResource.mComputeDirectIrradianceShader.SetupCompute();
	gCommandList->Dispatch(gAtmosphere.mResource.mIrradianceTexture.mWidth / 8, gAtmosphere.mResource.mIrradianceTexture.mHeight / 8, gAtmosphere.mResource.mIrradianceTexture.mDepth);
}

void Atmosphere::ComputeSingleScattering()
{
	gAtmosphere.mResource.mComputeSingleScatteringShader.SetupCompute();
	gCommandList->Dispatch(gAtmosphere.mResource.mScatteringTexture.mWidth / 8, gAtmosphere.mResource.mScatteringTexture.mHeight / 8, gAtmosphere.mResource.mScatteringTexture.mDepth);
}

void Atmosphere::ComputeScatteringDensity(glm::uint scattering_order)
{
	gAtmosphere.mResource.mComputeScatteringDensityShader.SetupCompute();

	AtmosphereConstantsPerDraw atmosphere_per_draw = {};
	atmosphere_per_draw.mScatteringOrder = scattering_order;
	gCommandList->SetComputeRoot32BitConstants(1, sizeof(AtmosphereConstantsPerDraw) / 4, &atmosphere_per_draw, 0);

	gCommandList->Dispatch(gAtmosphere.mResource.mDeltaScatteringDensityTexture.mWidth / 8, gAtmosphere.mResource.mDeltaScatteringDensityTexture.mHeight / 8, gAtmosphere.mResource.mDeltaScatteringDensityTexture.mDepth);
}

void Atmosphere::ComputeIndirectIrradiance(glm::uint scattering_order)
{
	gAtmosphere.mResource.mComputeIndirectIrradianceShader.SetupCompute();

	AtmosphereConstantsPerDraw atmosphere_per_draw = {};
	atmosphere_per_draw.mScatteringOrder = scattering_order - 1;
	gCommandList->SetComputeRoot32BitConstants(1, sizeof(AtmosphereConstantsPerDraw) / 4, &atmosphere_per_draw, 0);

	gCommandList->Dispatch(gAtmosphere.mResource.mIrradianceTexture.mWidth / 8, gAtmosphere.mResource.mIrradianceTexture.mHeight / 8, gAtmosphere.mResource.mIrradianceTexture.mDepth);
}

void Atmosphere::AccumulateMultipleScattering()
{
	gAtmosphere.mResource.mComputeMultipleScatteringShader.SetupCompute();
	gCommandList->Dispatch(gAtmosphere.mResource.mScatteringTexture.mWidth / 8, gAtmosphere.mResource.mScatteringTexture.mHeight / 8, gAtmosphere.mResource.mScatteringTexture.mDepth);
}

void Atmosphere::ComputeMultipleScattering(glm::uint scattering_order)
{
	ComputeScatteringDensity(scattering_order);
	ComputeIndirectIrradiance(scattering_order);
	AccumulateMultipleScattering();
}

void Atmosphere::Precompute()
{
	static Profile sAtmosphereProfileCache = gAtmosphere.mProfile;
	if (memcmp(&sAtmosphereProfileCache, &gAtmosphere.mProfile, sizeof(Profile)) != 0)
	{
		sAtmosphereProfileCache = gAtmosphere.mProfile;
		mRecomputeRequested = true;
	}

	if (!mRecomputeRequested && !mRecomputeEveryFrame)
		return;

	ComputeTransmittance();
	ComputeDirectIrradiance();
	ComputeSingleScattering();

	for (glm::uint scattering_order = 2; scattering_order <= gAtmosphere.mProfile.mScatteringOrder; scattering_order++)
		ComputeMultipleScattering(scattering_order);

	// Reset accumulation
	gPerFrameConstantBuffer.mReset = true;

	mRecomputeRequested = false;
}

void Atmosphere::Compute()
{
	// [Hillaire20]
	TransLUT();
	NewMultiScatCS();
	SkyViewLut();
	CameraVolumes();
}

void Atmosphere::Validate()
{
	auto diff = [](const Texture& inComputed, const Texture& inExpected, const Texture& inOutput)
	{
		BarrierScope computed_scope(gCommandList, inComputed.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		BarrierScope expected_scope(gCommandList, inExpected.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		BarrierScope output_scope(gCommandList, inOutput.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		Shader& shader = inExpected.mDepth == 1 ? gDiffTexture2DShader : gDiffTexture3DShader;
		shader.SetupCompute(gUniversalHeap.Get(), false);

		gCommandList->SetComputeRoot32BitConstant(0, inComputed.mResourceHeapIndex, 0);
		gCommandList->SetComputeRoot32BitConstant(0, inExpected.mResourceHeapIndex, 1);
		gCommandList->SetComputeRoot32BitConstant(0, inOutput.mResourceHeapIndex, 2);

		gCommandList->Dispatch((inExpected.mWidth + 7) / 8, (inExpected.mHeight + 7) / 8, inExpected.mDepth);
	};

	diff(
		gAtmosphere.mResource.mTransmittanceTex,
		gAtmosphere.mResource.mValidation.mTransmittanceTexExpected,
		gAtmosphere.mResource.mValidation.mTransmittanceTex);
	diff(
		gAtmosphere.mResource.mMultiScattTex,
		gAtmosphere.mResource.mValidation.mMultiScattTexExpected,
		gAtmosphere.mResource.mValidation.mMultiScattTex);
	diff(
		gAtmosphere.mResource.mSkyViewLutTex,
		gAtmosphere.mResource.mValidation.mSkyViewLutTexExpected,
		gAtmosphere.mResource.mValidation.mSkyViewLutTex);
	diff(
		gAtmosphere.mResource.mAtmosphereCameraScatteringVolume,
		gAtmosphere.mResource.mValidation.mAtmosphereCameraScatteringVolumeExpected,
		gAtmosphere.mResource.mValidation.mAtmosphereCameraScatteringVolume);
}

void Atmosphere::TransLUT()
{
	gAtmosphere.mResource.mTransLUTShader.SetupCompute();
	gCommandList->Dispatch((gAtmosphere.mResource.mTransmittanceTex.mWidth + 7) / 8, (gAtmosphere.mResource.mTransmittanceTex.mHeight + 7) / 8, 1);
}

void Atmosphere::NewMultiScatCS()
{
	gAtmosphere.mResource.mNewMultiScatCSShader.SetupCompute();
	gCommandList->Dispatch(gAtmosphere.mResource.mMultiScattTex.mWidth, gAtmosphere.mResource.mMultiScattTex.mHeight, 1);
}

void Atmosphere::SkyViewLut()
{
	gAtmosphere.mResource.mSkyViewLutShader.SetupCompute();
	gCommandList->Dispatch((gAtmosphere.mResource.mSkyViewLutTex.mWidth + 7) / 8, (gAtmosphere.mResource.mSkyViewLutTex.mHeight + 7) / 8, 1);
}

void Atmosphere::CameraVolumes()
{
	gAtmosphere.mResource.mCameraVolumesShader.SetupCompute();
	gCommandList->Dispatch((gAtmosphere.mResource.mAtmosphereCameraScatteringVolume.mWidth + 7) / 8, (gAtmosphere.mResource.mAtmosphereCameraScatteringVolume.mHeight + 7) / 8, gAtmosphere.mResource.mAtmosphereCameraScatteringVolume.mDepth);
}

void Atmosphere::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp(static_cast<UINT>(sizeof(AtmosphereConstants)), static_cast<UINT>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gAtmosphere.mResource.mConstantUploadBuffer)));
		gAtmosphere.mResource.mConstantUploadBuffer->SetName(L"Atmosphere.Constant");
		gAtmosphere.mResource.mConstantUploadBuffer->Map(0, nullptr, (void**)&gAtmosphere.mResource.mConstantUploadBufferPointer);
	}

	// Texture
	{
		for (auto&& texture : gAtmosphere.mResource.mTextures)
			texture.Initialize();

		for (auto&& texture : gAtmosphere.mResource.mValidation.mTextures)
			texture.Initialize();
	}

	// Shader Binding
	{
		// [NOTE] Strictly speaking, binding same texture as UAV and SRV at the same time is not ok, and there should be barriers for state transition

		std::vector<Shader::DescriptorInfo> common_binding;
		{
			// CBV
			common_binding.push_back(gConstantGPUBuffer.Get());

			// CBV
			common_binding.push_back(gAtmosphere.mResource.mConstantUploadBuffer.Get());

			// UAV
			for (auto&& texture : gAtmosphere.mResource.mTextures)
				common_binding.push_back(texture.mResource.Get());

			// SRV
			for (auto&& texture : gAtmosphere.mResource.mTextures)
				common_binding.push_back(texture.mResource.Get());
		}

		for (auto&& shader : gAtmosphere.mResource.mShaders)
			shader.InitializeDescriptors(common_binding);
	}
}

void Atmosphere::Finalize()
{
	gAtmosphere.mResource.Reset();
}

void Atmosphere::UpdateImGui()
{
#define SMALL_BUTTON(func) if (ImGui::SmallButton(NAMEOF(func).c_str())) func(gAtmosphere.mProfile);

	if (ImGui::TreeNodeEx("Mode", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < static_cast<int>(AtmosphereMode::Count); i++)
		{
			const auto& name = nameof::nameof_enum(static_cast<AtmosphereMode>(i));
			if (name[0] == '_')
			{
				ImGui::NewLine();
				continue;
			} 

			if (i != 0)
				ImGui::SameLine();

			if (ImGui::RadioButton(name.data(), static_cast<int>(gAtmosphere.mProfile.mMode) == i))
				gAtmosphere.mProfile.mMode = static_cast<AtmosphereMode>(i);
		}

		{
			ImGui::PushID("Preset");

			SMALL_BUTTON(Profile::Preset::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::Preset::Hillaire20);

			ImGui::PopID();
		}

		ImGui::Checkbox("Scene Unit is Kilometer, otherwise Meter", &gAtmosphere.mProfile.mSceneInKilometer);
		ImGui::Checkbox("Aerial Perspective", &gAtmosphere.mProfile.mAerialPerspective);

		if (gAtmosphere.mProfile.mMode == AtmosphereMode::ConstantColor)
		{
			ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&gAtmosphere.mProfile.mConstantColor), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		}

		if (gAtmosphere.mProfile.mMode == AtmosphereMode::Bruneton17)
		{
			ImGui::SliderInt("Scattering Order", reinterpret_cast<int*>(&gAtmosphere.mProfile.mScatteringOrder), 1, 8);
			ImGui::Checkbox("Recompute Every Frame", &mRecomputeEveryFrame);
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Sun"))
	{
		ImGui::SliderFloat("Luminance Scale", &gPerFrameConstantBuffer.mSunLuminanceScale, 0.0f, 1.0f);
		ImGui::SliderAngle("Azimuth Angle", &gPerFrameConstantBuffer.mSunAzimuth, 0.0f, 360.0f);
		ImGui::SliderAngle("Zenith Angle", &gPerFrameConstantBuffer.mSunZenith, 0.0f, 180.0f);

		ImGui::Text(gAtmosphere.mProfile.mShowSolarIrradianceAsLuminance ? "Solar Irradiance (klm)" : "Solar Irradiance (kW)");
		float scale = gAtmosphere.mProfile.mShowSolarIrradianceAsLuminance ? kSunLuminousEfficacy : 1.0f;
		glm::vec3 solar_value = gAtmosphere.mProfile.mSolarIrradiance * scale;
		if (ImGui::ColorEdit3("", &solar_value[0], ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
			gAtmosphere.mProfile.mSolarIrradiance = solar_value / scale;
		ImGui::Checkbox("Use Luminance", &gAtmosphere.mProfile.mShowSolarIrradianceAsLuminance);
		ImGui::SameLine();
		ImGui::Text("(Luminous Efficacy = %.2f lm/W)", kSunLuminousEfficacy);

		if (ImGui::SmallButton("Bruneton17")) Profile::SolarIrradianceReference::Bruneton17(gAtmosphere.mProfile);
		ImGui::SameLine(); 
		if (ImGui::SmallButton("Bruneton17Constant")) Profile::SolarIrradianceReference::Bruneton17Constant(gAtmosphere.mProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("Hillaire20")) Profile::SolarIrradianceReference::Hillaire20(gAtmosphere.mProfile);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Geometry"))
	{
		ImGui::PushItemWidth(200);

		ImGui::SliderDouble("Earth Radius (km)", &gAtmosphere.mProfile.mBottomRadius, 1.0, 10000.0);
		ImGui::SliderDouble("AtmosphereConstants Thickness (km)", &gAtmosphere.mProfile.mAtmosphereThickness, 1.0, 100.0);

		ImGui::PopItemWidth();

		SMALL_BUTTON(Profile::GeometryReference::Bruneton17);
		ImGui::SameLine();
		SMALL_BUTTON(Profile::GeometryReference::Yusov13);
		ImGui::SameLine();
		SMALL_BUTTON(Profile::GeometryReference::Hillaire20);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Ground"))
	{
		ImGui::ColorEdit3("Albedo (Precomputed)", &gAtmosphere.mProfile.mGroundAlbedo[0], ImGuiColorEditFlags_Float);
		ImGui::ColorEdit3("Albedo (Runtime)", &gAtmosphere.mProfile.mRuntimeGroundAlbedo[0], ImGuiColorEditFlags_Float);

		if (ImGui::SmallButton("Bruneton17")) Profile::GroundReference::Bruneton17(gAtmosphere.mProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("Hillaire20")) Profile::GroundReference::Hillaire20(gAtmosphere.mProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("UE4")) Profile::GroundReference::UE4(gAtmosphere.mProfile);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Coefficients & Density"))
	{
		struct DensityPlot
		{
			float mMin = 0.0;
			float mMax = 1.0;
			int mCount = 100;
			DensityProfile* mProfile = nullptr;

			static float Func(void* inData, int inIndex)
			{
				DensityPlot& plot = *static_cast<DensityPlot*>(inData);
				float altitude = (inIndex * 1.0f / (plot.mCount - 1)) * (plot.mMax - plot.mMin) + plot.mMin;
				DensityProfileLayer& layer = altitude < plot.mProfile->mLayer0.mWidth ? plot.mProfile->mLayer0 : plot.mProfile->mLayer1;

				float density = layer.mExpTerm * exp(layer.mExpScale * altitude) + layer.mLinearTerm * altitude + layer.mConstantTerm;
				return glm::clamp(density, 0.0f, 1.0f);
			}
		};

		static DensityProfile* sDensityProfile = nullptr;
		auto popup_density_profile = []()
		{
			if (ImGui::BeginPopup("DensityProfile") && sDensityProfile != nullptr)
			{
				ImGui::SliderFloat("ExpTerm", &sDensityProfile->mLayer1.mExpTerm, -2.0f, 2.0f);
				ImGui::SliderFloat("ExpScale", &sDensityProfile->mLayer1.mExpScale, -2.0f, 2.0f);
				ImGui::SliderFloat("LinearTerm", &sDensityProfile->mLayer1.mLinearTerm, -2.0f, 2.0f);
				ImGui::SliderFloat("ConstantTerm", &sDensityProfile->mLayer1.mConstantTerm, -2.0f, 2.0f);

				ImGui::NewLine();

				float scale_height = -1.0f / sDensityProfile->mLayer1.mExpScale;
				if (ImGui::SliderFloat("Scale Height (km) -> ExpScale", &scale_height, 0.1f, 100.0f))
					sDensityProfile->mLayer1.mExpScale = -1.0f / scale_height;

				ImGui::EndPopup();
			}
		};

		if (ImGui::TreeNodeEx("Rayleigh"))
		{
			ImGui::Checkbox("Enable", &gAtmosphere.mProfile.mEnableRayleigh);

			ImGui::SliderDouble3("Scattering  (/m)", &gAtmosphere.mProfile.mRayleighScatteringCoefficient.x, 1.0e-5, 1.0e-1, "%.3e");

			DensityPlot plot;
			plot.mMax = static_cast<float>(gAtmosphere.mProfile.mAtmosphereThickness);
			plot.mProfile = &gAtmosphere.mProfile.mRayleighDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}
			popup_density_profile();

			SMALL_BUTTON(Profile::RayleighReference::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::RayleighReference::Bruneton08);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::RayleighReference::Preetham99);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::RayleighReference::Yusov13);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::RayleighReference::Hillaire20);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Mie"))
		{
			ImGui::Checkbox("Enable", &gAtmosphere.mProfile.mEnableMie);

			ImGui::SliderDouble3("Extinction (/km)", &gAtmosphere.mProfile.mMieExtinctionCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
			ImGui::SliderDouble3("Scattering (/km)", &gAtmosphere.mProfile.mMieScatteringCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
			ImGui::SliderDouble("Phase Function G", &gAtmosphere.mProfile.mMiePhaseFunctionG, -1.0f, 1.0f);

			DensityPlot plot;
			plot.mMin = 0.0f;
			plot.mMax = static_cast<float>(gAtmosphere.mProfile.mAtmosphereThickness);
			plot.mCount = 500;
			plot.mProfile = &gAtmosphere.mProfile.mMieDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}
			popup_density_profile();

			SMALL_BUTTON(Profile::MieReference::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::MieReference::Bruneton08);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::MieReference::Yusov13);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::MieReference::Hillaire20);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Ozone"))
		{
			ImGui::Checkbox("Enable", &gAtmosphere.mProfile.mEnableOzone);

			ImGui::SliderDouble3("Absorption (/km)", &gAtmosphere.mProfile.mOZoneAbsorptionCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
			
			DensityPlot plot;
			plot.mMin = 0.0f;
			plot.mMax = static_cast<float>(gAtmosphere.mProfile.mAtmosphereThickness);
			plot.mCount = 500;
			plot.mProfile = &gAtmosphere.mProfile.mOzoneDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}

			if (ImGui::BeginPopup("DensityProfile") && sDensityProfile != nullptr)
			{
				bool value_changed = false;

				value_changed |= ImGui::SliderDouble("Ozone Bottom Altitude (m)", &gAtmosphere.mProfile.mOzoneBottomAltitude, 1000.0, 100000.0);
				value_changed |= ImGui::SliderDouble("Ozone Mid Altitude (m)", &gAtmosphere.mProfile.mOzoneMidAltitude, 1000.0, 100000.0);
				value_changed |= ImGui::SliderDouble("Ozone Top Altitude (m)", &gAtmosphere.mProfile.mOzoneTopAltitude, 1000.0, 100000.0);

				if (value_changed)
					Profile::OzoneReference::UpdateDensityProfile(gAtmosphere.mProfile);

				ImGui::EndPopup();
			}

			SMALL_BUTTON(Profile::OzoneReference::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::OzoneReference::Hillaire20);

			ImGui::TreePop();
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Encoding"))
	{
		if (gAtmosphere.mProfile.mMode == AtmosphereMode::Bruneton17)
		{
			ImGui::Text(nameof::nameof_enum_type<AtmosphereMuSEncodingMode>().data());
			for (int i = 0; i < static_cast<int>(AtmosphereMuSEncodingMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<AtmosphereMuSEncodingMode>(i));
				if (i != 0)
					ImGui::SameLine();

				if (ImGui::RadioButton(name.data(), static_cast<int>(gAtmosphere.mProfile.mMuSEncodingMode) == i))
					gAtmosphere.mProfile.mMuSEncodingMode = static_cast<AtmosphereMuSEncodingMode>(i);
			}
		}
		else
		{
			ImGui::Text("N/A");
		}

		ImGui::TreePop();
	}

	ImGuiShowTextures(gAtmosphere.mResource.mTextures, "Texture", ImGuiTreeNodeFlags_DefaultOpen);
	ImGuiShowTextures(gAtmosphere.mResource.mValidation.mTextures, "Validation", ImGuiTreeNodeFlags_None);
}

Atmosphere gAtmosphere;
