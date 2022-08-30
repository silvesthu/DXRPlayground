#include "Atmosphere.h"
#include "Color.h"
#include "ImGui/imgui_impl_helper.h"

void Atmosphere::Update()
{
	UpdateProfile();

	for (auto&& textures : mRuntime.mTexturesSet)
		for (auto&& texture : textures)
			texture.Update();

	for (auto&& textures : mRuntime.mValidationTexturesSet)
		for (auto&& texture : textures)
			texture.Update();

	switch (mProfile.mMode)
	{
	case AtmosphereMode::Bruneton17:			mRuntime.mBruneton17.Update(mProfile); break;
	case AtmosphereMode::Hillaire20:			mRuntime.mHillaire20.Update(mProfile); break;
	case AtmosphereMode::Wilkie21:				mRuntime.mWilkie21.Update(mProfile); break;
	default:									break;
	}

	if (mProfile.mMode != AtmosphereMode::Wilkie21 && mRuntime.mWilkie21.mSplitScreen != 0)
		mRuntime.mWilkie21.Update(mProfile);
}

void Atmosphere::UpdateProfile()
{
	AtmosphereConstants* constants				= static_cast<AtmosphereConstants*>(mRuntime.mConstantUploadBufferPointer);

	auto inv_m_to_inv_km						= [](glm::f64 inInvM) { return static_cast<float>(inInvM * 1000.0); };

	constants->mBottomRadius					= static_cast<float>(mProfile.BottomRadius());
	constants->mTopRadius						= static_cast<float>(mProfile.TopRadius());
	constants->mSceneScale						= mRuntime.mSceneInKilometer ? 1.0f : 0.001f;

	constants->mMode							= mProfile.mMode;
	constants->mMuSEncodingMode					= mRuntime.mBruneton17.mMuSEncodingMode;
	constants->mSliceCount						= mRuntime.mSliceCount;

	constants->mConstantColor					= mProfile.mConstantColor;

	constants->mSolarIrradiance					= mProfile.mSolarIrradiance;
	constants->mSunAngularRadius				= static_cast<float>(mProfile.kSunAngularRadius);

	constants->mHillaire20SkyViewInLuminance	= mRuntime.mHillaire20.mSkyViewInLuminance;
	constants->mWilkie21SkyViewSplitScreen		= mRuntime.mWilkie21.mSplitScreen;
	constants->mAerialPerspective				= mRuntime.mAerialPerspective ? 1 : 0;
	constants->mGroundAlbedo					= mProfile.mGroundAlbedo;
	constants->mRuntimeGroundAlbedo				= mProfile.mRuntimeGroundAlbedo;

	// Density Profile
	{
		// Rayleigh
		{
			// Scattering Coefficient
			constants->mRayleighScattering		= mProfile.mEnableRayleigh ? mProfile.mRayleighScatteringCoefficient : glm::dvec3(1e-9);

			// Extinction Coefficient
			constants->mRayleighExtinction		= mProfile.mEnableRayleigh ? mProfile.mRayleighScatteringCoefficient : glm::dvec3(1e-9);

			// Density
			constants->mRayleighDensity			= mProfile.mRayleighDensityProfile;
		}

		// Mie
		{
			// Scattering Coefficient
			constants->mMieScattering			= mProfile.mEnableMie ? mProfile.mMieScatteringCoefficient : glm::dvec3(1e-9);

			// Extinction Coefficient
			constants->mMieExtinction			= mProfile.mEnableMie ? mProfile.mMieExtinctionCoefficient : glm::dvec3(1e-9);

			// Phase function
			constants->mMiePhaseFunctionG		= static_cast<float>(mProfile.mMiePhaseFunctionG);

			// Density
			constants->mMieDensity				= mProfile.mMieDensityProfile;
		}

		// Ozone
		{
			// Extinction Coefficient
			constants->mOzoneExtinction			= mProfile.mEnableOzone ? mProfile.mOZoneAbsorptionCoefficient : glm::dvec3();

			// Density
			constants->mOzoneDensity				= mProfile.mOzoneDensityProfile;
		}
	}
}

void Atmosphere::Runtime::Bruneton17::Update(const Profile& inProfile)
{
	static Atmosphere::Profile sAtmosphereProfileCache = inProfile;
	if (memcmp(&sAtmosphereProfileCache, &inProfile, sizeof(Atmosphere::Profile)) != 0)
	{
		sAtmosphereProfileCache = inProfile;

		// Reset accumulation
		gPerFrameConstantBuffer.mReset = true;

		// Recompute
		mRecomputeRequested = true;
	}

	if (mRecomputeRequested || mRecomputeEveryFrame)
	{
		ComputeTransmittance();
		ComputeDirectIrradiance();
		ComputeSingleScattering();

		for (glm::uint scattering_order = 2; scattering_order <= mScatteringOrder; scattering_order++)
			ComputeMultipleScattering(scattering_order);
	}

	mRecomputeRequested = false;
}

void Atmosphere::Runtime::Bruneton17::ComputeTransmittance()
{
	mComputeTransmittanceShader.SetupCompute();
	gCommandList->Dispatch(mTransmittanceTexture.mWidth / 8, mTransmittanceTexture.mHeight / 8, mTransmittanceTexture.mDepth);
}

void Atmosphere::Runtime::Bruneton17::ComputeDirectIrradiance()
{
	mComputeDirectIrradianceShader.SetupCompute();
	gCommandList->Dispatch(mIrradianceTexture.mWidth / 8, mIrradianceTexture.mHeight / 8, mIrradianceTexture.mDepth);
}

void Atmosphere::Runtime::Bruneton17::ComputeSingleScattering()
{
	mComputeSingleScatteringShader.SetupCompute();
	gCommandList->Dispatch(mScatteringTexture.mWidth / 8, mScatteringTexture.mHeight / 8, mScatteringTexture.mDepth);
}

void Atmosphere::Runtime::Bruneton17::ComputeScatteringDensity(glm::uint scattering_order)
{
	mComputeScatteringDensityShader.SetupCompute();

	AtmosphereConstantsPerDraw atmosphere_per_draw = {};
	atmosphere_per_draw.mScatteringOrder = scattering_order;
	gCommandList->SetComputeRoot32BitConstants(1, sizeof(AtmosphereConstantsPerDraw) / 4, &atmosphere_per_draw, 0);

	gCommandList->Dispatch(mDeltaScatteringDensityTexture.mWidth / 8, mDeltaScatteringDensityTexture.mHeight / 8, mDeltaScatteringDensityTexture.mDepth);
}

void Atmosphere::Runtime::Bruneton17::ComputeIndirectIrradiance(glm::uint scattering_order)
{
	mComputeIndirectIrradianceShader.SetupCompute();

	AtmosphereConstantsPerDraw atmosphere_per_draw = {};
	atmosphere_per_draw.mScatteringOrder = scattering_order - 1;
	gCommandList->SetComputeRoot32BitConstants(1, sizeof(AtmosphereConstantsPerDraw) / 4, &atmosphere_per_draw, 0);

	gCommandList->Dispatch(mIrradianceTexture.mWidth / 8, mIrradianceTexture.mHeight / 8, mIrradianceTexture.mDepth);
}

void Atmosphere::Runtime::Bruneton17::AccumulateMultipleScattering()
{
	mComputeMultipleScatteringShader.SetupCompute();
	gCommandList->Dispatch(mScatteringTexture.mWidth / 8, mScatteringTexture.mHeight / 8, mScatteringTexture.mDepth);
}

void Atmosphere::Runtime::Bruneton17::ComputeMultipleScattering(glm::uint scattering_order)
{
	ComputeScatteringDensity(scattering_order);
	ComputeIndirectIrradiance(scattering_order);
	AccumulateMultipleScattering();
}

void Atmosphere::Runtime::Hillaire20::Update(const Profile& inProfile)
{
	(void)inProfile;

	TransLUT();
	NewMultiScatCS();
	SkyViewLut();
	CameraVolumes();

	Validate();
}

void Atmosphere::Runtime::Hillaire20::TransLUT()
{
	mTransLUTShader.SetupCompute();
	gCommandList->Dispatch((mTransmittanceTex.mWidth + 7) / 8, (mTransmittanceTex.mHeight + 7) / 8, 1);
}

void Atmosphere::Runtime::Hillaire20::NewMultiScatCS()
{
	mNewMultiScatCSShader.SetupCompute();
	gCommandList->Dispatch(mMultiScattTex.mWidth, mMultiScattTex.mHeight, 1);
}

void Atmosphere::Runtime::Hillaire20::SkyViewLut()
{
	mSkyViewLutShader.SetupCompute();
	gCommandList->Dispatch((mSkyViewLut.mWidth + 7) / 8, (mSkyViewLut.mHeight + 7) / 8, 1);
}

void Atmosphere::Runtime::Hillaire20::CameraVolumes()
{
	mCameraVolumesShader.SetupCompute();
	gCommandList->Dispatch((mAtmosphereCameraScatteringVolume.mWidth + 7) / 8, (mAtmosphereCameraScatteringVolume.mHeight + 7) / 8, mAtmosphereCameraScatteringVolume.mDepth);
}

void Atmosphere::Runtime::Hillaire20::Validate()
{
	auto diff = [](const Texture& inComputed, const Texture& inExpected, const Texture& inOutput)
	{
		BarrierScope computed_scope(gCommandList, inComputed.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		BarrierScope expected_scope(gCommandList, inExpected.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		BarrierScope output_scope(gCommandList, inOutput.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		Shader& shader = inExpected.mDepth == 1 ? gDiffTexture2DShader : gDiffTexture3DShader;
		shader.SetupCompute(gGetFrameContext().mDescriptorHeap.mHeap.Get(), false);

		gAssert(inComputed.mUAVIndex > DescriptorIndex::NullCount);
		gAssert(inExpected.mUAVIndex > DescriptorIndex::NullCount);
		gAssert(inOutput.mUAVIndex > DescriptorIndex::NullCount);

		gCommandList->SetComputeRoot32BitConstant(0, static_cast<UINT>(inComputed.mUAVIndex), 0);
		gCommandList->SetComputeRoot32BitConstant(0, static_cast<UINT>(inExpected.mUAVIndex), 1);
		gCommandList->SetComputeRoot32BitConstant(0, static_cast<UINT>(inOutput.mUAVIndex), 2);

		gCommandList->Dispatch((inExpected.mWidth + 7) / 8, (inExpected.mHeight + 7) / 8, inExpected.mDepth);
	};

	diff(mTransmittanceTex,						mTransmittanceTexExpected,						mTransmittanceTexDiff);
	diff(mMultiScattTex,						mMultiScattExpected,							mMultiScattDiff);
	diff(mSkyViewLut,							mSkyViewLutExpected,							mSkyViewLutDiff);
	diff(mAtmosphereCameraScatteringVolume,		mAtmosphereCameraScatteringVolumeExpected,		mAtmosphereCameraScatteringVolumeDiff);
}

void Atmosphere::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp(static_cast<UINT>(sizeof(AtmosphereConstants)), static_cast<UINT>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mRuntime.mConstantUploadBuffer)));
		mRuntime.mConstantUploadBuffer->SetName(L"Atmosphere.Constant");
		mRuntime.mConstantUploadBuffer->Map(0, nullptr, (void**)&mRuntime.mConstantUploadBufferPointer);
	}

	// Texture
	{
		for (auto&& texture_set : mRuntime.mTexturesSet)
			for (auto&& texture : texture_set)
				texture.Initialize();

		for (auto&& texture_set : mRuntime.mValidationTexturesSet)
			for (auto&& texture : texture_set)
			texture.Initialize();
	}

	// Shader Binding
	{
		std::vector<Shader::DescriptorInfo> common_binding;
		{
			// CBV
			common_binding.push_back(gConstantGPUBuffer.Get());

			// CBV
			common_binding.push_back(mRuntime.mConstantUploadBuffer.Get());

			// UAV
			for (auto&& textures : mRuntime.mTexturesSet)
				for (auto&& texture : textures)
					common_binding.push_back(texture.mResource.Get());

			// SRV
			for (auto&& textures : mRuntime.mTexturesSet)
				for (auto&& texture : textures)
					common_binding.push_back(texture.mResource.Get());
		}

		for (auto&& shaders : mRuntime.mShadersSet)
			for (auto&& shader : shaders)
				shader.InitializeDescriptors(common_binding);
	}
}

void Atmosphere::Finalize()
{
	mRuntime.Reset();
}

void Atmosphere::UpdateImGui()
{
#define SMALL_BUTTON(func) if (ImGui::SmallButton(NAMEOF(func).c_str())) func(mProfile);

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

			if (ImGui::RadioButton(name.data(), static_cast<int>(mProfile.mMode) == i))
				mProfile.mMode = static_cast<AtmosphereMode>(i);
		}

		{
			ImGui::PushID("Preset");

			SMALL_BUTTON(Profile::Preset::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::Preset::Hillaire20);

			ImGui::PopID();
		}

		if (mProfile.mMode == AtmosphereMode::ConstantColor)
		{
			ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&mProfile.mConstantColor), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		}
		
		if (mProfile.mMode == AtmosphereMode::Bruneton17)
		{
			ImGui::SliderInt("Scattering Order", reinterpret_cast<int*>(&mRuntime.mBruneton17.mScatteringOrder), 1, 8);
			ImGui::Checkbox("Recompute Every Frame", &mRuntime.mBruneton17.mRecomputeEveryFrame);
		}

		ImGui::Checkbox("Scene Unit is Kilometer, otherwise Meter", &mRuntime.mSceneInKilometer);
		ImGui::Checkbox("Aerial Perspective", &mRuntime.mAerialPerspective);
		ImGui::Checkbox("[Hillaire20] SkyView in Luminance", &mRuntime.mHillaire20.mSkyViewInLuminance);
		if (ImGui::Button("[Wilkie21] Bake -> Split Screen")) mRuntime.mWilkie21.mBakeRequested = true;
		ImGui::SameLine();
		if (ImGui::RadioButton("Off", mRuntime.mWilkie21.mSplitScreen == 0)) mRuntime.mWilkie21.mSplitScreen = 0;
		ImGui::SameLine();
		if (ImGui::RadioButton("Left", mRuntime.mWilkie21.mSplitScreen == 1)) mRuntime.mWilkie21.mSplitScreen = 1;
		ImGui::SameLine();
		if (ImGui::RadioButton("Right", mRuntime.mWilkie21.mSplitScreen == 2)) mRuntime.mWilkie21.mSplitScreen = 2;

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Wilkie21"))
	{
		ImGui::SliderDouble("Turbidity", &mProfile.mWilkie21.mTurbidity, 1.37, 3.7);
		ImGui::InputDouble("Visibility", &mRuntime.mWilkie21.mVisibility, 0.0, 0.0, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::SliderDouble("Albedo", &mProfile.mWilkie21.mAlbedo, 0.0, 1.0);

		if (ImGui::TreeNodeEx("Hosek", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputDouble3("Zenith Spectrum (as XYZ)", &mRuntime.mWilkie21.mHosekZenithSpectrum.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputDouble3("Zenith XYZ", &mRuntime.mWilkie21.mHosekZenithXYZ.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputDouble3("Zenith RGB", &mRuntime.mWilkie21.mHosekZenithRGB.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputDouble3("Solar Spectrum (as XYZ)", &mRuntime.mWilkie21.mHosekSolarSpectrum.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
				
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Prague", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputDouble3("Zenith Spectrum (as XYZ)", &mRuntime.mWilkie21.mPragueZenithSpectrum.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputDouble3("Zenith Spectrum (as RGB)", &mRuntime.mWilkie21.mPragueZenithRGB.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputDouble3("Solar Spectrum (as XYZ)", &mRuntime.mWilkie21.mPragueSolarSpectrum.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputDouble3("Transmittance (as XYZ)", &mRuntime.mWilkie21.mPragueTransmittance.x, "%.3f", ImGuiInputTextFlags_ReadOnly);

			ImGui::TreePop();
		}

		ImGui::Checkbox("Bake Hosek", &mRuntime.mWilkie21.mBakeHosek);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Sun"))
	{
		ImGui::SliderFloat("Luminance Scale", &gPerFrameConstantBuffer.mSolarLuminanceScale, 0.0f, 1.0f);
		ImGui::SliderAngle("Azimuth Angle", &gPerFrameConstantBuffer.mSunAzimuth, 0.0f, 360.0f);
		ImGui::SliderAngle("Zenith Angle", &gPerFrameConstantBuffer.mSunZenith, 0.0f, 180.0f);

		ImGui::Text(mProfile.mShowSolarIrradianceAsLuminance ? "Solar Irradiance (klm)" : "Solar Irradiance (kW)");
		float scale = mProfile.mShowSolarIrradianceAsLuminance ? kSolarLuminousEfficacy : 1.0f;
		glm::vec3 solar_value = mProfile.mSolarIrradiance * scale;
		if (ImGui::ColorEdit3("", &solar_value[0], ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
			mProfile.mSolarIrradiance = solar_value / scale;
		ImGui::Checkbox("Use Luminance", &mProfile.mShowSolarIrradianceAsLuminance);
		ImGui::SameLine();
		ImGui::Text("(Luminous Efficacy = %.2f lm/W)", kSolarLuminousEfficacy);

		if (ImGui::SmallButton("Bruneton17")) Profile::SolarIrradianceReference::Bruneton17(mProfile);
		ImGui::SameLine(); 
		if (ImGui::SmallButton("Bruneton17Constant")) Profile::SolarIrradianceReference::Bruneton17Constant(mProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("Hillaire20")) Profile::SolarIrradianceReference::Hillaire20(mProfile);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Geometry"))
	{
		ImGui::PushItemWidth(200);

		ImGui::SliderDouble("Earth Radius (km)", &mProfile.mBottomRadius, 1.0, 10000.0);
		ImGui::SliderDouble("AtmosphereConstants Thickness (km)", &mProfile.mAtmosphereThickness, 1.0, 100.0);

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
		ImGui::ColorEdit3("Albedo (Precomputed)", &mProfile.mGroundAlbedo[0], ImGuiColorEditFlags_Float);
		ImGui::ColorEdit3("Albedo (Runtime)", &mProfile.mRuntimeGroundAlbedo[0], ImGuiColorEditFlags_Float);

		if (ImGui::SmallButton("Bruneton17")) Profile::GroundReference::Bruneton17(mProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("Hillaire20")) Profile::GroundReference::Hillaire20(mProfile);
		ImGui::SameLine();
		if (ImGui::SmallButton("UE4")) Profile::GroundReference::UE4(mProfile);

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
			ImGui::Checkbox("Enable", &mProfile.mEnableRayleigh);

			ImGui::SliderDouble3("Scattering  (/m)", &mProfile.mRayleighScatteringCoefficient.x, 1.0e-5, 1.0e-1, "%.3e");

			DensityPlot plot;
			plot.mMax = static_cast<float>(mProfile.mAtmosphereThickness);
			plot.mProfile = &mProfile.mRayleighDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}
			popup_density_profile();

			SMALL_BUTTON(Profile::RayleighReference::Bruneton08);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::RayleighReference::Preetham99);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::RayleighReference::Yusov13);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::RayleighReference::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::RayleighReference::Hillaire20);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Mie"))
		{
			ImGui::Checkbox("Enable", &mProfile.mEnableMie);

			ImGui::SliderDouble3("Extinction (/km)", &mProfile.mMieExtinctionCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
			ImGui::SliderDouble3("Scattering (/km)", &mProfile.mMieScatteringCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
			ImGui::SliderDouble("Phase Function G", &mProfile.mMiePhaseFunctionG, -1.0f, 1.0f);

			DensityPlot plot;
			plot.mMin = 0.0f;
			plot.mMax = static_cast<float>(mProfile.mAtmosphereThickness);
			plot.mCount = 500;
			plot.mProfile = &mProfile.mMieDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}
			popup_density_profile();

			SMALL_BUTTON(Profile::MieReference::Bruneton08);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::MieReference::Yusov13);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::MieReference::Bruneton17);
			ImGui::SameLine();
			SMALL_BUTTON(Profile::MieReference::Hillaire20);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Ozone"))
		{
			ImGui::Checkbox("Enable", &mProfile.mEnableOzone);

			ImGui::SliderDouble3("Absorption (/km)", &mProfile.mOZoneAbsorptionCoefficient[0], 1.0e-5, 1.0e-1, "%.3e");
		
			DensityPlot plot;
			plot.mMin = 0.0f;
			plot.mMax = static_cast<float>(mProfile.mAtmosphereThickness);
			plot.mCount = 500;
			plot.mProfile = &mProfile.mOzoneDensityProfile;
			ImGui::PlotLines("Density", DensityPlot::Func, &plot, plot.mCount, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sDensityProfile = plot.mProfile;
				ImGui::OpenPopup("DensityProfile");
			}

			if (ImGui::BeginPopup("DensityProfile") && sDensityProfile != nullptr)
			{
				bool value_changed = false;

				value_changed |= ImGui::SliderDouble("Ozone Bottom Altitude (m)", &mProfile.mOzoneBottomAltitude, 1000.0, 100000.0);
				value_changed |= ImGui::SliderDouble("Ozone Mid Altitude (m)", &mProfile.mOzoneMidAltitude, 1000.0, 100000.0);
				value_changed |= ImGui::SliderDouble("Ozone Top Altitude (m)", &mProfile.mOzoneTopAltitude, 1000.0, 100000.0);

				if (value_changed)
					Profile::OzoneReference::UpdateDensityProfile(mProfile);

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
		if (mProfile.mMode == AtmosphereMode::Bruneton17)
		{
			ImGui::Text(nameof::nameof_enum_type<AtmosphereMuSEncodingMode>().data());
			for (int i = 0; i < static_cast<int>(AtmosphereMuSEncodingMode::Count); i++)
			{
				const auto& name = nameof::nameof_enum(static_cast<AtmosphereMuSEncodingMode>(i));
				if (i != 0)
					ImGui::SameLine();

				if (ImGui::RadioButton(name.data(), static_cast<int>(mRuntime.mBruneton17.mMuSEncodingMode) == i))
					mRuntime.mBruneton17.mMuSEncodingMode = static_cast<AtmosphereMuSEncodingMode>(i);
			}
		}
		else
		{
			ImGui::Text("N/A");
		}

		ImGui::TreePop();
	}

	ImGuiShowTextures(mRuntime.mBruneton17.mTextures,				"Atmosphere.Bruneton17",				ImGuiTreeNodeFlags_None);
	ImGuiShowTextures(mRuntime.mHillaire20.mTextures,				"Atmosphere.Hillaire20",				ImGuiTreeNodeFlags_None);
	ImGuiShowTextures(mRuntime.mHillaire20.mValidationTextures,		"Atmosphere.Hillaire20.Validation",		ImGuiTreeNodeFlags_None);
	ImGuiShowTextures(mRuntime.mWilkie21.mTextures,					"Atmosphere.Wilkie21",					ImGuiTreeNodeFlags_None);
}

Atmosphere gAtmosphere;

#include "Thirdparty/ArHosekSkyModel/ArHosekSkyModel.h"
#include "Thirdparty/ArPragueSkyModelGround/ArPragueSkyModelGround.h"

struct SkyModel
{
	struct Parameters
	{
		double mSunElevation = 0.0;
		double mTurbidity = 0.0;
		double mAlbedo = 0.0;
	};
	
	void Reset(const SkyModel::Parameters& inParameters, double& outVisibility)
	{
		Free();

		mHosek = arhosekskymodelstate_alloc_init(
			inParameters.mSunElevation,
			inParameters.mTurbidity,
			inParameters.mAlbedo);

		mHosekXYZ = arhosek_xyz_skymodelstate_alloc_init(
			inParameters.mTurbidity,
			inParameters.mAlbedo,
			inParameters.mSunElevation);

		mHosekRGB = arhosek_rgb_skymodelstate_alloc_init(
			inParameters.mTurbidity,
			inParameters.mAlbedo,
			inParameters.mSunElevation);

		outVisibility = 7487.f * exp(-3.41f * inParameters.mTurbidity) + 117.1f * exp(-0.4768f * inParameters.mTurbidity);

		mPrague = arpragueskymodelground_state_alloc_init(
			"Asset/ArPragueSkyModelGround/SkyModelDataset.dat",
			inParameters.mSunElevation,
			outVisibility,
			inParameters.mAlbedo);
	}

	void Free()
	{
		if (mHosek != nullptr)
			arhosekskymodelstate_free(mHosek);

		if (mHosekXYZ != nullptr)
			arhosekskymodelstate_free(mHosekXYZ);

		if (mHosekRGB != nullptr)
			arhosekskymodelstate_free(mHosekRGB);
		
		if (mPrague != nullptr)
			arpragueskymodelground_state_free(mPrague);
	}
	
	~SkyModel()
	{
		Free();
	}
	
	ArHosekSkyModelState* mHosek = nullptr;
	ArHosekSkyModelState* mHosekXYZ = nullptr;
	ArHosekSkyModelState* mHosekRGB = nullptr;
	ArPragueSkyModelGroundState* mPrague = nullptr;
};
SkyModel gArPragueSkyModelGround;

void Atmosphere::Runtime::Wilkie21::Update(const Profile& inProfile)
{
	if (!mBakeRequested)
		return;
	
	double sun_elevation			= glm::pi<double>() / 2.0 - gPerFrameConstantBuffer.mSunZenith;
	double sun_azimuth				= gPerFrameConstantBuffer.mSunAzimuth;
	glm::dvec3 view_direction		= glm::dvec3(0, 0, 1);
	glm::dvec3 up_direction			= glm::dvec3(0, 0, 1);	
	double theta					= 0.0;
	double gamma					= 0.0;
	double shadow					= 0.0;
	arpragueskymodelground_compute_angles(sun_elevation, sun_azimuth, &view_direction[0], &up_direction[0], &theta, &gamma, &shadow);

	SkyModel::Parameters parameters = { sun_elevation, inProfile.mWilkie21.mTurbidity, inProfile.mWilkie21.mAlbedo };
	gArPragueSkyModelGround.Reset(parameters, mVisibility);

	Color::Spectrum hosek_sky_radiance;
	Color::Spectrum hosek_solar_radiance;
	Color::Spectrum prague_sky_radiance;
	Color::Spectrum prague_solar_radiance;
	Color::Spectrum prague_transmittance;
	for (int i = 0; i < Color::LambdaCount; i++)
	{
		if (Color::LambdaMin + i <= 720)
		{
			hosek_sky_radiance.mEnergy[i] = arhosekskymodel_radiance(gArPragueSkyModelGround.mHosek, theta, gamma, Color::LambdaMin + i);
			hosek_solar_radiance.mEnergy[i] = arhosekskymodel_solar_radiance(gArPragueSkyModelGround.mHosek, theta, gamma, Color::LambdaMin + i);	
		}
		prague_sky_radiance.mEnergy[i] = arpragueskymodelground_sky_radiance(gArPragueSkyModelGround.mPrague, theta, gamma, shadow, Color::LambdaMin + i);
		prague_solar_radiance.mEnergy[i] = arpragueskymodelground_solar_radiance(gArPragueSkyModelGround.mPrague, theta, Color::LambdaMin + i);
		prague_transmittance.mEnergy[i] = arpragueskymodelground_transmittance(gArPragueSkyModelGround.mPrague, theta, Color::LambdaMin + i, PSMG_ATMO_WIDTH);
	}

	// Still working on the units
	//
	// ArHosekSkyModel.h says
	// > Also, the output of the XYZ model is now no longer scaled to the range [0...1]. Instead, it is
	// > the result of a simple conversion from spectral data via the CIE 2 degree
	// > standard observer matching functions. Therefore, after multiplication
	// > with 683 lm / W, the Y channel now corresponds to luminance in lm.
	// So at least mHosekZenithXYZ should give the luminance value as cd/m^2
	//
	// Then mHosekZenithSpectrum is likely to require no normalization? But why?
	// > The coefficients of the spectral model are now scaled so that the output
	// > is given in physical units: W / (m^-2 * sr * nm)
	//
	// Assume mPragueZenithSpectrum is given in same unit as mHosekZenithSpectrum. At least the magnitude looks right.
	// Also mPragueSolarSpectrum will match 1.6x10^9 cd/m^2 from internet.
	// But how to explain mHosekSolarSpectrum? It seems more of sun+sky, while mPragueSolarSpectrum is more like sun. Just ignore it for now...
	//
	// Other reference:
	// - Mitsuba divides radiance by 106.856980 for both Spectrum and XYZ of Hosek model.
	//   - https://github.com/mitsuba-renderer/mitsuba/blob/cfeb7766e7a1513492451f35dc65b86409655a7b/src/emitters/sky.cpp#L434
	// - clear-sky-models
	//   - https://github.com/ebruneton/clear-sky-models
	
	mHosekZenithSpectrum = Color::SpectrumToXYZ(hosek_sky_radiance, false).mData * Color::MaxLuminousEfficacy;
	mHosekSolarSpectrum = Color::SpectrumToXYZ(hosek_solar_radiance, false).mData * Color::MaxLuminousEfficacy;
	mHosekZenithXYZ =
	{
		arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekXYZ, theta, gamma, 0) * Color::MaxLuminousEfficacy,
		arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekXYZ, theta, gamma, 1) * Color::MaxLuminousEfficacy,
		arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekXYZ, theta, gamma, 2) * Color::MaxLuminousEfficacy,
	};
	mHosekZenithRGB =
	{
		arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekRGB, theta, gamma, 0) * Color::MaxLuminousEfficacy,
		arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekRGB, theta, gamma, 1) * Color::MaxLuminousEfficacy,
		arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekRGB, theta, gamma, 2) * Color::MaxLuminousEfficacy,
	};
	mPragueZenithSpectrum = Color::SpectrumToXYZ(prague_sky_radiance, false).mData * Color::MaxLuminousEfficacy;
	mPragueZenithRGB = Color::XYZToRGB({ mPragueZenithSpectrum }, Color::RGBColorSpace::Rec709).mData;
	mPragueSolarSpectrum = Color::SpectrumToXYZ(prague_solar_radiance, false).mData * Color::MaxLuminousEfficacy;
	mPragueTransmittance = Color::SpectrumToXYZ(prague_transmittance, true).mData;

	// Bake SkyView
	{
		// Use same uv mapping of Hillaire20
		auto UvToSkyViewLutParams = [](glm::vec2 uv)
		{
#define NONLINEARSKYVIEWLUT 1
			{
				float viewZenithCosAngle = 0.0f;
				float lightViewCosAngle = 0.0f;
			
				float CosBeta = 0.0f;				// GroundToHorizonCos
				float Beta = acos(CosBeta);
				float ZenithHorizonAngle = glm::pi<float>() - Beta;

				if (uv.y < 0.5f)
				{
					float coord = 2.0f * uv.y;
					coord = 1.0f - coord;
#if NONLINEARSKYVIEWLUT
					coord *= coord;
#endif
					coord = 1.0f - coord;
					viewZenithCosAngle = cos(ZenithHorizonAngle * coord);
				}
				else
				{
					float coord = uv.y * 2.0f - 1.0f;
#if NONLINEARSKYVIEWLUT
					coord *= coord;
#endif
					viewZenithCosAngle = cos(ZenithHorizonAngle + Beta * coord);
				}

				float coord = uv.x;
				coord *= coord;
				lightViewCosAngle = -(coord * 2.0f - 1.0f);

				return glm::vec2(viewZenithCosAngle, lightViewCosAngle);
			}
		};

		mSkyView.mUploadData.resize(mSkyView.GetSubresourceSize());
		glm::uint64* pixels = reinterpret_cast<glm::uint64*>(mSkyView.mUploadData.data());
	
		int width = static_cast<int>(mSkyView.mWidth);
		int height = static_cast<int>(mSkyView.mHeight);
	
#pragma omp parallel for
		for (int h = 0; h < height; h++)
		{
			for (int w = 0; w < width; w++)
			{
				glm::vec2 uv = {w * 1.0f / (width - 1) , h * 1.0f / (height - 1)};
				glm::vec2 params = UvToSkyViewLutParams(uv);

				float viewZenithCosAngle = params.x;
				float lightViewCosAngle = params.y;
				float viewZenithSinAngle = sqrt(1 - viewZenithCosAngle * viewZenithCosAngle);
				view_direction = float3(
					viewZenithSinAngle * lightViewCosAngle,
					viewZenithSinAngle * sqrt(1.0 - lightViewCosAngle * lightViewCosAngle),
					viewZenithCosAngle);
				arpragueskymodelground_compute_angles(sun_elevation, sun_azimuth, &view_direction[0], &up_direction[0], &theta, &gamma, &shadow);

				Color::RGB luminance;
				if (mBakeHosek)
				{
					luminance =
						{ glm::vec3(
							arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekRGB, theta, gamma, 0) * Color::MaxLuminousEfficacy,
							arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekRGB, theta, gamma, 1) * Color::MaxLuminousEfficacy,
							arhosek_tristim_skymodel_radiance(gArPragueSkyModelGround.mHosekRGB, theta, gamma, 2) * Color::MaxLuminousEfficacy)
						};
				}
				else
				{
					for (int i = 0; i < Color::LambdaCount; i++)
						prague_sky_radiance.mEnergy[i] = arpragueskymodelground_sky_radiance(gArPragueSkyModelGround.mPrague, theta, gamma, shadow, Color::LambdaMin + i);
					luminance = Color::XYZToRGB({ Color::SpectrumToXYZ(prague_sky_radiance, false).mData * Color::MaxLuminousEfficacy }, Color::RGBColorSpace::Rec709);
				}
				luminance.mData *= kPreExposure;
			
				glm::uint64 pixel = glm::packHalf2x16({luminance.mData.b, 1.0});
				pixel = pixel << 32;
				pixel |= glm::packHalf2x16({luminance.mData.r, luminance.mData.g});
				pixels[(h * width + w)] = pixel;
			}
		};
	}

	mBakeRequested = false;
}