#include "Common.h"
#include "Atmosphere.h"

#include "ImGui/imgui_impl_dx12.h"
#include "ImGui/imgui_impl_helper.h"

AtmosphereProfile gAtmosphereProfile;
PrecomputedAtmosphereScatteringResources gPrecomputedAtmosphereScatteringResources;
PrecomputedAtmosphereScattering gPrecomputedAtmosphereScattering;

void PrecomputedAtmosphereScattering::Update()
{
	AtmosphereConstants* contants				= static_cast<AtmosphereConstants*>(gPrecomputedAtmosphereScatteringResources.mConstantUploadBufferPointer);

	auto inv_m_to_inv_km						= [](glm::f64 inInvM) { return static_cast<float>(inInvM * 1000.0); };

	contants->mBottomRadius						= static_cast<float>(gAtmosphereProfile.BottomRadius());
	contants->mTopRadius						= static_cast<float>(gAtmosphereProfile.TopRadius());
	contants->mSceneScale						= gAtmosphereProfile.mSceneInKilometer ? 1.0f : 0.001f;

	contants->mMode								= gAtmosphereProfile.mMode;
	contants->mMuSEncodingMode					= gAtmosphereProfile.mMuSEncodingMode;
	contants->mSliceCount						= gPrecomputedAtmosphereScatteringResources.mSliceCount;

	contants->mConstantColor					= gAtmosphereProfile.mConstantColor;

	contants->mSolarIrradiance					= gAtmosphereProfile.mSolarIrradiance;
	contants->mPrecomputeWithSolarIrradiance	= gAtmosphereProfile.mPrecomputeWithSolarIrradiance;
	contants->mSunAngularRadius					= static_cast<float>(gAtmosphereProfile.kSunAngularRadius);

	contants->mAerialPerspective				= (gAtmosphereProfile.mMode != AtmosphereMode::RaymarchAtmosphereOnly && gAtmosphereProfile.mAerialPerspective) ? 1.0f : 0.0f;
	contants->mGroundAlbedo						= gAtmosphereProfile.mGroundAlbedo;
	contants->mRuntimeGroundAlbedo				= gAtmosphereProfile.mRuntimeGroundAlbedo;

	// Density Profile
	{
		// Rayleigh
		{
			// Scattering Coefficient
			contants->mRayleighScattering		= gAtmosphereProfile.mEnableRayleigh ? gAtmosphereProfile.mRayleighScatteringCoefficient : glm::dvec3(1e-9);

			// Extinction Coefficient
			contants->mRayleighExtinction		= gAtmosphereProfile.mEnableRayleigh ? gAtmosphereProfile.mRayleighScatteringCoefficient : glm::dvec3(1e-9);

			// Density
			contants->mRayleighDensity			= gAtmosphereProfile.mRayleighDensityProfile;
		}

		// Mie
		{
			// Scattering Coefficient
			contants->mMieScattering			= gAtmosphereProfile.mEnableMie ? gAtmosphereProfile.mMieScatteringCoefficient : glm::dvec3(1e-9);

			// Extinction Coefficient
			contants->mMieExtinction			= gAtmosphereProfile.mEnableMie ? gAtmosphereProfile.mMieExtinctionCoefficient : glm::dvec3(1e-9);

			// Phase function
			contants->mMiePhaseFunctionG		= static_cast<float>(gAtmosphereProfile.mMiePhaseFunctionG);

			// Density
			contants->mMieDensity				= gAtmosphereProfile.mMieDensityProfile;
		}

		// Ozone
		{
			// Extinction Coefficient
			contants->mOzoneExtinction			= gAtmosphereProfile.mEnableOzone ? gAtmosphereProfile.mOZoneAbsorptionCoefficient : glm::dvec3();

			// Density
			contants->mOzoneDensity				= gAtmosphereProfile.mOzoneDensityProfile;
		}
	}
}

void PrecomputedAtmosphereScattering::Load()
{
	for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mTextures)
		texture->Load();

	for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mValidation.mTextures)
		texture->Load();
}

void PrecomputedAtmosphereScattering::ComputeTransmittance()
{
	gPrecomputedAtmosphereScatteringResources.mComputeTransmittanceShader.SetupCompute();
	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture.mDepth);
}

void PrecomputedAtmosphereScattering::ComputeDirectIrradiance()
{
	gPrecomputedAtmosphereScatteringResources.mComputeDirectIrradianceShader.SetupCompute();
	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mDepth);
}

void PrecomputedAtmosphereScattering::ComputeSingleScattering()
{
	gPrecomputedAtmosphereScatteringResources.mComputeSingleScatteringShader.SetupCompute();
	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mDepth);
}

void PrecomputedAtmosphereScattering::ComputeScatteringDensity(glm::uint scattering_order)
{
	gPrecomputedAtmosphereScatteringResources.mComputeScatteringDensityShader.SetupCompute();

	AtmosphereConstantsPerDraw atmosphere_per_draw = {};
	atmosphere_per_draw.mScatteringOrder = scattering_order;
	gCommandList->SetComputeRoot32BitConstants(1, sizeof(AtmosphereConstantsPerDraw) / 4, &atmosphere_per_draw, 0);

	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mDeltaScatteringDensityTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mDeltaScatteringDensityTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mDeltaScatteringDensityTexture.mDepth);
}

void PrecomputedAtmosphereScattering::ComputeIndirectIrradiance(glm::uint scattering_order)
{
	gPrecomputedAtmosphereScatteringResources.mComputeIndirectIrradianceShader.SetupCompute();

	AtmosphereConstantsPerDraw atmosphere_per_draw = {};
	atmosphere_per_draw.mScatteringOrder = scattering_order - 1;
	gCommandList->SetComputeRoot32BitConstants(1, sizeof(AtmosphereConstantsPerDraw) / 4, &atmosphere_per_draw, 0);

	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mDepth);
}

void PrecomputedAtmosphereScattering::AccumulateMultipleScattering()
{
	gPrecomputedAtmosphereScatteringResources.mComputeMultipleScatteringShader.SetupCompute();
	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mDepth);
}

void PrecomputedAtmosphereScattering::ComputeMultipleScattering(glm::uint scattering_order)
{
	ComputeScatteringDensity(scattering_order);
	ComputeIndirectIrradiance(scattering_order);
	AccumulateMultipleScattering();
}

void PrecomputedAtmosphereScattering::Precompute()
{
	static AtmosphereProfile sAtmosphereProfileCache = gAtmosphereProfile;
	if (memcmp(&sAtmosphereProfileCache, &gAtmosphereProfile, sizeof(AtmosphereProfile)) != 0)
	{
		sAtmosphereProfileCache = gAtmosphereProfile;
		mRecomputeRequested = true;
	}

	if (!mRecomputeRequested && !mRecomputeEveryFrame)
		return;

	ComputeTransmittance();
	ComputeDirectIrradiance();
	ComputeSingleScattering();

	for (glm::uint scattering_order = 2; scattering_order <= gAtmosphereProfile.mScatteringOrder; scattering_order++)
		ComputeMultipleScattering(scattering_order);

	// Reset accumulation
	gPerFrameConstantBuffer.mReset = true;

	mRecomputeRequested = false;
}

void PrecomputedAtmosphereScattering::Compute()
{
	// [Hillaire20]
	TransLUT();
	NewMultiScatCS();
	SkyViewLut();
	CameraVolumes();
}

void PrecomputedAtmosphereScattering::Validate()
{
	auto validate = [](const Texture& inComputed, const Texture& inExpected)
	{
		BarrierScope computed_scope(gCommandList, inComputed.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		BarrierScope expected_scope(gCommandList, inExpected.mIntermediateResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		BarrierScope output_scope(gCommandList, inExpected.mResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		Shader& shader = inExpected.mDepth == 1 ? gDiffTexture2DShader : gDiffTexture3DShader;
		shader.SetupCompute(gUniversalHeap.Get(), false);

		gCommandList->SetComputeRoot32BitConstant(0, inComputed.mResourceHeapIndex, 0);
		gCommandList->SetComputeRoot32BitConstant(0, inExpected.mIntermediateResourceHeapIndex, 1);
		gCommandList->SetComputeRoot32BitConstant(0, inExpected.mResourceHeapIndex, 2);

		gCommandList->Dispatch((inExpected.mWidth + 7) / 8, (inExpected.mHeight + 7) / 8, inExpected.mDepth);
	};

	validate(
		gPrecomputedAtmosphereScatteringResources.mTransmittanceTex, 
		gPrecomputedAtmosphereScatteringResources.mValidation.mTransmittanceTex);
	validate(
		gPrecomputedAtmosphereScatteringResources.mMultiScattTex,
		gPrecomputedAtmosphereScatteringResources.mValidation.mMultiScattTex);
	validate(
		gPrecomputedAtmosphereScatteringResources.mSkyViewLutTex,
		gPrecomputedAtmosphereScatteringResources.mValidation.mSkyViewLutTex);
	validate(
		gPrecomputedAtmosphereScatteringResources.mAtmosphereCameraScatteringVolume,
		gPrecomputedAtmosphereScatteringResources.mValidation.mAtmosphereCameraScatteringVolume);
}

void PrecomputedAtmosphereScattering::TransLUT()
{
	gPrecomputedAtmosphereScatteringResources.mTransLUTShader.SetupCompute();
	gCommandList->Dispatch((gPrecomputedAtmosphereScatteringResources.mTransmittanceTex.mWidth + 7) / 8, (gPrecomputedAtmosphereScatteringResources.mTransmittanceTex.mHeight + 7) / 8, 1);
}

void PrecomputedAtmosphereScattering::NewMultiScatCS()
{
	gPrecomputedAtmosphereScatteringResources.mNewMultiScatCSShader.SetupCompute();
	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mMultiScattTex.mWidth, gPrecomputedAtmosphereScatteringResources.mMultiScattTex.mHeight, 1);
}

void PrecomputedAtmosphereScattering::SkyViewLut()
{
	gPrecomputedAtmosphereScatteringResources.mSkyViewLutShader.SetupCompute();
	gCommandList->Dispatch((gPrecomputedAtmosphereScatteringResources.mSkyViewLutTex.mWidth + 7) / 8, (gPrecomputedAtmosphereScatteringResources.mSkyViewLutTex.mHeight + 7) / 8, 1);
}

void PrecomputedAtmosphereScattering::CameraVolumes()
{
	gPrecomputedAtmosphereScatteringResources.mCameraVolumesShader.SetupCompute();
	gCommandList->Dispatch((gPrecomputedAtmosphereScatteringResources.mAtmosphereCameraScatteringVolume.mWidth + 7) / 8, (gPrecomputedAtmosphereScatteringResources.mAtmosphereCameraScatteringVolume.mHeight + 7) / 8, gPrecomputedAtmosphereScatteringResources.mAtmosphereCameraScatteringVolume.mDepth);
}

void PrecomputedAtmosphereScattering::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp(static_cast<UINT>(sizeof(AtmosphereConstants)), static_cast<UINT>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer)));
		gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->SetName(L"AtmosphereConstants.Constant");
		gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->Map(0, nullptr, (void**)&gPrecomputedAtmosphereScatteringResources.mConstantUploadBufferPointer);
	}

	// Texture
	{
		for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mTextures)
			texture->Initialize();

		for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mValidation.mTextures)
			texture->Initialize();
	}

	// Shader Binding
	{
		// [NOTE] Strictly speaking, binding same texture as UAV and SRV at the same time is not ok, and there should be barriers for state transition

		std::vector<Shader::DescriptorInfo> common_binding;
		{
			// CBV
			common_binding.push_back(gConstantGPUBuffer.Get());

			// CBV
			common_binding.push_back(gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer.Get());

			// UAV
			for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mTextures)
				common_binding.push_back(texture->mResource.Get());

			// SRV
			for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mTextures)
				common_binding.push_back(texture->mResource.Get());
		}

		for (auto&& shader : gPrecomputedAtmosphereScatteringResources.mShaders)
			shader->InitializeDescriptors(common_binding);
	}
}

void PrecomputedAtmosphereScattering::Finalize()
{
	gPrecomputedAtmosphereScatteringResources = {};
}

void PrecomputedAtmosphereScattering::UpdateImGui()
{
#define SMALL_BUTTON(func) if (ImGui::SmallButton(NAMEOF(func).c_str())) func(gAtmosphereProfile);

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

			if (ImGui::RadioButton(name.data(), static_cast<int>(gAtmosphereProfile.mMode) == i))
				gAtmosphereProfile.mMode = static_cast<AtmosphereMode>(i);
		}

		{
			ImGui::PushID("Preset");

			SMALL_BUTTON(AtmosphereProfile::Preset::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::Preset::Hillaire20);

			ImGui::PopID();
		}

		ImGui::Checkbox("Scene Unit is Kilometer, otherwise Meter", &gAtmosphereProfile.mSceneInKilometer);
		ImGui::Checkbox("Aerial Perspective", &gAtmosphereProfile.mAerialPerspective);

		if (gAtmosphereProfile.mMode == AtmosphereMode::ConstantColor)
		{
			ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&gAtmosphereProfile.mConstantColor), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		}

		if (gAtmosphereProfile.mMode == AtmosphereMode::Bruneton17)
		{
			ImGui::SliderInt("Scattering Order", reinterpret_cast<int*>(&gAtmosphereProfile.mScatteringOrder), 1, 8);
			ImGui::Checkbox("Recompute Every Frame", &mRecomputeEveryFrame);
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Sun"))
	{
		ImGui::SliderFloat("Luminance Scale", &gPerFrameConstantBuffer.mSunLuminanceScale, 0.0f, 1.0f);
		ImGui::SliderAngle("Azimuth Angle", &gPerFrameConstantBuffer.mSunAzimuth, 0.0f, 360.0f);
		ImGui::SliderAngle("Zenith Angle", &gPerFrameConstantBuffer.mSunZenith, 0.0f, 180.0f);

		ImGui::Text(gAtmosphereProfile.mShowSolarIrradianceAsLuminance ? "Solar Irradiance (klm)" : "Solar Irradiance (kW)");
		float scale = gAtmosphereProfile.mShowSolarIrradianceAsLuminance ? kSunLuminousEfficacy : 1.0f;
		glm::vec3 solar_value = gAtmosphereProfile.mSolarIrradiance * scale;
		if (ImGui::ColorEdit3("", &solar_value[0], ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
			gAtmosphereProfile.mSolarIrradiance = solar_value / scale;
		ImGui::Checkbox("Use Luminance", &gAtmosphereProfile.mShowSolarIrradianceAsLuminance);
		ImGui::SameLine();
		ImGui::Text("(Luminous Efficacy = %.2f lm/W)", kSunLuminousEfficacy);
		ImGui::Checkbox("Precompute With Solar Irradiance", &gAtmosphereProfile.mPrecomputeWithSolarIrradiance);

		if (ImGui::SmallButton("Bruneton17")) AtmosphereProfile::SolarIrradianceReference::Bruneton17(gAtmosphereProfile);
		ImGui::SameLine(); 
		if (ImGui::SmallButton("Bruneton17Constant")) AtmosphereProfile::SolarIrradianceReference::Bruneton17Constant(gAtmosphereProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("Hillaire20")) AtmosphereProfile::SolarIrradianceReference::Hillaire20(gAtmosphereProfile);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Geometry"))
	{
		ImGui::PushItemWidth(200);

		ImGui::SliderDouble("Earth Radius (km)", &gAtmosphereProfile.mBottomRadius, 1.0, 10000.0);
		ImGui::SliderDouble("AtmosphereConstants Thickness (km)", &gAtmosphereProfile.mAtmosphereThickness, 1.0, 100.0);

		ImGui::PopItemWidth();

		SMALL_BUTTON(AtmosphereProfile::GeometryReference::Bruneton17);
		ImGui::SameLine();
		SMALL_BUTTON(AtmosphereProfile::GeometryReference::Yusov13);
		ImGui::SameLine();
		SMALL_BUTTON(AtmosphereProfile::GeometryReference::Hillaire20);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Ground"))
	{
		ImGui::ColorEdit3("Albedo (Precomputed)", &gAtmosphereProfile.mGroundAlbedo[0], ImGuiColorEditFlags_Float);
		ImGui::ColorEdit3("Albedo (Runtime)", &gAtmosphereProfile.mRuntimeGroundAlbedo[0], ImGuiColorEditFlags_Float);

		if (ImGui::SmallButton("Bruneton17")) AtmosphereProfile::GroundReference::Bruneton17(gAtmosphereProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("Hillaire20")) AtmosphereProfile::GroundReference::Hillaire20(gAtmosphereProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("UE4")) AtmosphereProfile::GroundReference::UE4(gAtmosphereProfile);

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
			ImGui::Checkbox("Enable", &gAtmosphereProfile.mEnableRayleigh);

			ImGui::SliderDouble3("Scattering  (/m)", &gAtmosphereProfile.mRayleighScatteringCoefficient.x, 1.0e-5, 1.0e-1, "%.3e");

			DensityPlot plot;
			plot.mMax = static_cast<float>(gAtmosphereProfile.mAtmosphereThickness);
			plot.mProfile = &gAtmosphereProfile.mRayleighDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}
			popup_density_profile();

			SMALL_BUTTON(AtmosphereProfile::RayleighReference::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::RayleighReference::Bruneton08);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::RayleighReference::Preetham99);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::RayleighReference::Yusov13);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::RayleighReference::Hillaire20);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Mie"))
		{
			ImGui::Checkbox("Enable", &gAtmosphereProfile.mEnableMie);

			ImGui::SliderDouble3("Extinction (/km)", &gAtmosphereProfile.mMieExtinctionCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
			ImGui::SliderDouble3("Scattering (/km)", &gAtmosphereProfile.mMieScatteringCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
			ImGui::SliderDouble("Phase Function G", &gAtmosphereProfile.mMiePhaseFunctionG, -1.0f, 1.0f);

			DensityPlot plot;
			plot.mMin = 0.0f;
			plot.mMax = static_cast<float>(gAtmosphereProfile.mAtmosphereThickness);
			plot.mCount = 500;
			plot.mProfile = &gAtmosphereProfile.mMieDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}
			popup_density_profile();

			SMALL_BUTTON(AtmosphereProfile::MieReference::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::MieReference::Bruneton08);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::MieReference::Yusov13);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::MieReference::Hillaire20);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Ozone"))
		{
			ImGui::Checkbox("Enable", &gAtmosphereProfile.mEnableOzone);

			ImGui::SliderDouble3("Absorption (/km)", &gAtmosphereProfile.mOZoneAbsorptionCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
			
			DensityPlot plot;
			plot.mMin = 0.0f;
			plot.mMax = static_cast<float>(gAtmosphereProfile.mAtmosphereThickness);
			plot.mCount = 500;
			plot.mProfile = &gAtmosphereProfile.mOzoneDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}

			if (ImGui::BeginPopup("DensityProfile") && sDensityProfile != nullptr)
			{
				bool value_changed = false;

				value_changed |= ImGui::SliderDouble("Ozone Bottom Altitude (m)", &gAtmosphereProfile.mOzoneBottomAltitude, 1000.0, 100000.0);
				value_changed |= ImGui::SliderDouble("Ozone Mid Altitude (m)", &gAtmosphereProfile.mOzoneMidAltitude, 1000.0, 100000.0);
				value_changed |= ImGui::SliderDouble("Ozone Top Altitude (m)", &gAtmosphereProfile.mOzoneTopAltitude, 1000.0, 100000.0);

				if (value_changed)
					AtmosphereProfile::OzoneReference::UpdateDensityProfile(gAtmosphereProfile);

				ImGui::EndPopup();
			}

			SMALL_BUTTON(AtmosphereProfile::OzoneReference::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(AtmosphereProfile::OzoneReference::Hillaire20);

			ImGui::TreePop();
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Encoding"))
	{
		if (gAtmosphereProfile.mMode == AtmosphereMode::Bruneton17)
		{
			ImGui::Text(nameof::nameof_enum_type<AtmosphereMuSEncodingMode>().data());
			for (int i = 0; i < static_cast<int>(AtmosphereMuSEncodingMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<AtmosphereMuSEncodingMode>(i));
				if (i != 0)
					ImGui::SameLine();

				if (ImGui::RadioButton(name.data(), static_cast<int>(gAtmosphereProfile.mMuSEncodingMode) == i))
					gAtmosphereProfile.mMuSEncodingMode = static_cast<AtmosphereMuSEncodingMode>(i);
			}
		}
		else
		{
			ImGui::Text("N/A");
		}

		ImGui::TreePop();
	}

	ImGuiShowTextures(gPrecomputedAtmosphereScatteringResources.mTextures, "Texture", ImGuiTreeNodeFlags_DefaultOpen);
	ImGuiShowTextures(gPrecomputedAtmosphereScatteringResources.mValidation.mTextures, "Validation", ImGuiTreeNodeFlags_None);
}
