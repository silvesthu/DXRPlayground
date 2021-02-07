#include "Common.h"
#include "Atmosphere.h"

#include "ImGui/imgui_impl_dx12.h"
#include "ImGui/imgui_impl_helper.h"

AtmosphereProfile gAtmosphereProfile;
PrecomputedAtmosphereScattering gPrecomputedAtmosphereScattering;
PrecomputedAtmosphereScatteringResources gPrecomputedAtmosphereScatteringResources;

namespace UnitHelper
{
	template <typename ToType, typename FromType>
	static ToType stMeterToKilometer(const FromType& inMeter)
	{
		return ToType(inMeter * 1e-3);
	}

	template <typename ToType, typename FromType>
	static ToType sNanometerToMeter(const FromType& inNanometer)
	{
		return ToType(inNanometer * 1e-9);
	}

	template <typename ToType, typename FromType>
	static ToType sInverseMeterToInverseKilometer(const FromType& inInverseMeter)
	{
		return ToType(inInverseMeter * 1e3);
	}

	template <typename ToType, typename FromType>
	static ToType sInverseNanometerToInverseKilometer(const FromType& inInverseNanometer)
	{
		return ToType(inInverseNanometer * 1e12);
	}
}

void PrecomputedAtmosphereScattering::Update()
{
	ShaderType::Atmosphere* atmosphere = static_cast<ShaderType::Atmosphere*>(gPrecomputedAtmosphereScatteringResources.mConstantUploadBufferPointer);

	float dummy = 0.0f;

	auto inv_m_to_inv_km							= [](glm::f64 inInvM) { return static_cast<float>(inInvM * 1000.0); };

	atmosphere->mBottomRadius						= UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.BottomRadius());
	atmosphere->mTopRadius							= UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.TopRadius());

	atmosphere->mSolarIrradiance					= gAtmosphereProfile.mUseConstantSolarIrradiance ? gAtmosphereProfile.kConstantSolarIrradiance : gAtmosphereProfile.kSolarIrradiance;
	atmosphere->mSunAngularRadius					= static_cast<float>(gAtmosphereProfile.kSunAngularRadius);

	atmosphere->mXSliceCount						= gPrecomputedAtmosphereScatteringResources.mXSliceCount;

	atmosphere->mGroundAlbedo						= gAtmosphereProfile.mGroundAlbedo;

	// Density Profile: { Width, ExpTerm, ExpScale, LinearTerm, ConstantTerm }
	{
		// Rayleigh
		{
			switch (gAtmosphereProfile.mRayleighMode)
			{
			case AtmosphereProfile::RayleighMode::Precomputed:
				// [Bruneton08] 2.1 [REK*04] Table 3
				gAtmosphereProfile.mRayleighScatteringCoefficient = gAtmosphereProfile.mRayleighScatteringCoefficient_;
				break;
			case AtmosphereProfile::RayleighMode::Bruneton08Impl:
				// demo.cc
				gAtmosphereProfile.mRayleighScatteringCoefficient = gAtmosphereProfile.kRayleigh / glm::pow(UnitHelper::sNanometerToMeter<glm::dvec3>(gAtmosphereProfile.kLambda), glm::dvec3(4.0));
				break;
			case AtmosphereProfile::RayleighMode::PSS99:
			{
				// [PSS99] A.3
				constexpr double pi = glm::pi<double>();
				double n = 1.0003; // index of refraction of air
				double N = 2.545e25; // number of molecules per unit volume
				double p_n = 0.035; // depolarization factor
				double kRayleigh =
					((8.0 * glm::pow(pi, 3.0) * glm::pow((n * n - 1.0), 2.0)) * (6.0 + 3.0 * p_n))
					/ // -----------------------------------------------------------------------------------------
					((3.0 * N) * (6.0 - 7.0 * p_n));
				gAtmosphereProfile.mRayleighScatteringCoefficient = kRayleigh / glm::pow(UnitHelper::sNanometerToMeter<glm::dvec3>(gAtmosphereProfile.kLambda), glm::dvec3(4.0));

				// [Note] [Bruneton08] 2.1 (1) might miss the right half? (6.0 + 3.0 * p_n) / (6.0 - 7.0 * p_n)

				// Total scattering coefficient = gAtmosphereProfile.mRayleighScatteringCoefficient = integral of angular scattering coefficient in all directions
				// Angular scattering coefficient = Total scattering coefficient * (1 + cos(theta)^2) * 3.0 / 2.0
			}
			break;
			default:
				break;
			}

			// Scattering Coefficient
			atmosphere->mRayleighScattering = UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mRayleighScatteringCoefficient);

			// Extinction Coefficient
			// Note that extinction = scattering + absorption
			// Air molecules do not absorb light
			atmosphere->mRayleighExtinction = atmosphere->mRayleighScattering;

			// Density: decrease exponentially
			// [Bruneton08] 2.1 (1), e^(-1/H_R)
			atmosphere->mRayleighDensity.mLayer0 = { dummy, dummy, dummy, dummy, dummy };
			atmosphere->mRayleighDensity.mLayer1 = { dummy, 1.0f, -1.0f / UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kRayleighScaleHeight), 0.0f, 0.0f };

			// How to get scale height?
			// https://en.wikipedia.org/wiki/Scale_height
		}

		// Mie
		{
			switch (gAtmosphereProfile.mMieMode)
			{
			case AtmosphereProfile::MieMode::Bruneton08Impl:
			{
				// [Bruneton08 Impl]
				// [TODO] Further reference?
				gAtmosphereProfile.mMieExtinctionCoefficient = gAtmosphereProfile.kMieAngstromBeta / gAtmosphereProfile.kMieScaleHeight * glm::pow(gAtmosphereProfile.kLambda, glm::dvec3(-gAtmosphereProfile.kMieAngstromAlpha));

				// [TODO] Why this is different from the paper
				gAtmosphereProfile.mMieScatteringCoefficient = gAtmosphereProfile.mMieExtinctionCoefficient * gAtmosphereProfile.kMieSingleScatteringAlbedo;
			}
			break;
			case AtmosphereProfile::MieMode::Bruneton08:
			{
				// [Bruneton08] Figure 6
				gAtmosphereProfile.mMieExtinctionCoefficient = gAtmosphereProfile.mMieExtinctionCoefficientPaper;
				gAtmosphereProfile.mMieScatteringCoefficient = gAtmosphereProfile.mMieScatteringCoefficientPaper;
			}
			break;
			default:
				assert(false);
				break;
			}

			// Scattering Coefficient
			// [Bruneton08] 2.1 (3), beta_M(0, lambda)
			atmosphere->mMieScattering = UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mMieScatteringCoefficient);

			// Phase function
			atmosphere->mMiePhaseFunctionG = static_cast<float>(gAtmosphereProfile.kMiePhaseFunctionG);

			// Extinction Coefficient
			atmosphere->mMieExtinction = UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mMieExtinctionCoefficient);

			// Density: decrease exponentially
			// [Bruneton08] 2.1 (3), e^(-1/H_M)
			atmosphere->mMieDensity.mLayer0 = { dummy, dummy, dummy, dummy, dummy };
			atmosphere->mMieDensity.mLayer1 = { dummy, 1.0f, -1.0f / UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kMieScaleHeight), 0.0f, 0.0f };
		}

		// Ozone
		{
			// Density: increase linearly, then decrease linearly
			float ozone_bottom_altitude = UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kOzoneBottomAltitude);
			float ozone_mid_altitude = UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kOzoneMidAltitude);
			float ozone_top_altitude = UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kOzoneTopAltitude);
			float layer_0_linear_term, layer_0_constant_term, layer_1_linear_term, layer_1_constant_term;
			{
				// Altitude -> Density
				auto calculate_linear_term = [](float inX0, float inX1, float& outLinearTerm, float& outConstantTerm)
				{
					outLinearTerm = 1.0f / (inX1 - inX0);
					outConstantTerm = 1.0f * (0.0f - inX0) / (inX1 - inX0);
				};
				calculate_linear_term(ozone_bottom_altitude, ozone_mid_altitude, layer_0_linear_term, layer_0_constant_term);
				calculate_linear_term(ozone_top_altitude, ozone_mid_altitude, layer_1_linear_term, layer_1_constant_term);
			}

			gAtmosphereProfile.mOZoneAbsorptionCoefficient = gAtmosphereProfile.kMaxOzoneNumberDensity * gAtmosphereProfile.kOzoneCrossSection;

			if (gAtmosphereProfile.mEnableOzone)
			{
				atmosphere->mOzoneDensity.mLayer0 = { ozone_mid_altitude, 0.0f, 0.0f, layer_0_linear_term, layer_0_constant_term };
				atmosphere->mOzoneDensity.mLayer1 = { dummy, 0.0f, 0.0f, layer_1_linear_term, layer_1_constant_term };

				// Extinction Coefficient
				// Note that extinction = scattering + absorption
				// Ozone do not scatter light (?)
				atmosphere->mOzoneExtinction = UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mOZoneAbsorptionCoefficient);
			}
			else
			{
				atmosphere->mOzoneDensity.mLayer0 = { dummy, 0.0f, 0.0f, 0.0f, 0.0f };
				atmosphere->mOzoneDensity.mLayer1 = { dummy, 0.0f, 0.0f, 0.0f, 0.0f };

				atmosphere->mOzoneExtinction = glm::vec3(dummy);
			}
		}
	} // Density Profile
}

void PrecomputedAtmosphereScattering::Render()
{
	Update();
	Precompute();
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

void PrecomputedAtmosphereScattering::ComputeScatteringDensity(glm::uint inScatteringOrder)
{
	gPrecomputedAtmosphereScatteringResources.mComputeScatteringDensityShader.SetupCompute();

	ShaderType::AtmospherePerDraw atmosphere_per_draw = {};
	atmosphere_per_draw.mScatteringOrder = inScatteringOrder;
	gCommandList->SetComputeRoot32BitConstants(1, sizeof(ShaderType::AtmospherePerDraw) / 4, &atmosphere_per_draw, 0);

	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mDeltaScatteringDensityTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mDeltaScatteringDensityTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mDeltaScatteringDensityTexture.mDepth);
}

void PrecomputedAtmosphereScattering::ComputeIndirectIrradiance(glm::uint inScatteringOrder)
{
	gPrecomputedAtmosphereScatteringResources.mComputeIndirectIrradianceShader.SetupCompute();

	ShaderType::AtmospherePerDraw atmosphere_per_draw = {};
	atmosphere_per_draw.mScatteringOrder = inScatteringOrder - 1;
	gCommandList->SetComputeRoot32BitConstants(1, sizeof(ShaderType::AtmospherePerDraw) / 4, &atmosphere_per_draw, 0);

	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mDepth);
}

void PrecomputedAtmosphereScattering::AccumulateMultipleScattering()
{
	gPrecomputedAtmosphereScatteringResources.mComputeMultipleScatteringShader.SetupCompute();
	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mHeight / 8, gPrecomputedAtmosphereScatteringResources.mScatteringTexture.mDepth);
}

void PrecomputedAtmosphereScattering::ComputeMultipleScattering(glm::uint inScatteringOrder)
{
	ComputeScatteringDensity(inScatteringOrder);
	ComputeIndirectIrradiance(inScatteringOrder);
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

void PrecomputedAtmosphereScattering::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(ShaderType::Atmosphere), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer)));
		gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->SetName(L"Atmosphere.Constant");
		gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->Map(0, nullptr, (void**)&gPrecomputedAtmosphereScatteringResources.mConstantUploadBufferPointer);
	}

	// Texture
	{
		for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mTextures)
			texture->Initialize();
	}

	// Shader Binding
	{
		// [NOTE] Strictly speaking, binding same texture as UAV and SRV at the same time is not ok, and there should be barriers for state transition

		std::vector<Shader::DescriptorEntry> common_binding;
		{
			// CBV
			common_binding.push_back(gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->GetGPUVirtualAddress());

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
	ImGui::Text("Mode"); ImGui::SameLine();
	for (int i = 0; i < (int)BackgroundMode::Count; i++)
	{
		const auto& name = nameof::nameof_enum((BackgroundMode)i);

		ImGui::SameLine();
		if (ImGui::RadioButton(name.data(), (int)gPerFrameConstantBuffer.mBackgroundMode == i))
			gPerFrameConstantBuffer.mBackgroundMode = (BackgroundMode)i;
	}

	if (gPerFrameConstantBuffer.mBackgroundMode == BackgroundMode::Color)
		ImGui::ColorEdit3("Color", (float*)&gPerFrameConstantBuffer.mBackgroundColor);
	else
	{
		ImGui::SliderAngle("Sun Azimuth Angle", &gPerFrameConstantBuffer.mSunAzimuth, 0, 360.0f);
		ImGui::SliderAngle("Sun Zenith Angle", &gPerFrameConstantBuffer.mSunZenith, 0, 180.0f);
	}

	if (ImGui::TreeNodeEx("Geometry"))
	{
		ImGui::SliderDouble("Earth Radius (m)", &gAtmosphereProfile.kBottomRadius, 1000.0, 10000000.0);
		ImGui::SliderDouble("Atmosphere Thickness (m)", &gAtmosphereProfile.kAtmosphereThickness, 1000.0, 100000.0);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Coefficients & Density", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::TreeNodeEx("Rayleigh", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputDouble3("Scattering  (/m)", &gAtmosphereProfile.mRayleighScatteringCoefficient.x, "%.3e");

			ImGui::EnumRadioButton(AtmosphereProfile::RayleighMode::Precomputed, &gAtmosphereProfile.mRayleighMode);
			ImGui::SameLine();
			ImGui::EnumRadioButton(AtmosphereProfile::RayleighMode::Bruneton08Impl, &gAtmosphereProfile.mRayleighMode);
			ImGui::SameLine();
			ImGui::EnumRadioButton(AtmosphereProfile::RayleighMode::PSS99, &gAtmosphereProfile.mRayleighMode);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Mie", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::InputDouble3("Extinction (/m)", &gAtmosphereProfile.mMieExtinctionCoefficient.x, "%.3e");
			ImGui::InputDouble3("Scattering (/m)", &gAtmosphereProfile.mMieScatteringCoefficient.x, "%.3e");

			ImGui::SliderDouble("Phase Function G", &gAtmosphereProfile.kMiePhaseFunctionG, -1.0f, 1.0f);

			ImGui::EnumRadioButton(AtmosphereProfile::MieMode::Bruneton08Impl, &gAtmosphereProfile.mMieMode);
			ImGui::SameLine();
			ImGui::EnumRadioButton(AtmosphereProfile::MieMode::Bruneton08, &gAtmosphereProfile.mMieMode);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Ozone", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enable", &gAtmosphereProfile.mEnableOzone);

			ImGui::InputDouble3("Absorption (/m)", &gAtmosphereProfile.mOZoneAbsorptionCoefficient.x, "%.3e");

			ImGui::TreePop();
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Computation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Recompute Every Frame", &mRecomputeEveryFrame);
		ImGui::SliderInt("Scattering Order", (int*)&gAtmosphereProfile.mScatteringOrder, 1, 8);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushItemWidth(100);
		ImGui::SliderFloat("UI Scale", &mUIScale, 1.0, 4.0f);
		ImGui::PopItemWidth();
		ImGui::SameLine();
		ImGui::Checkbox("UI Flip Y", &mUIFlipY);

		static Texture* sTexture = nullptr;
		auto add_texture = [&](Texture& inTexture)
		{
			ImVec2 uv0 = ImVec2(0, 0);
			ImVec2 uv1 = ImVec2(1, 1);
			
			if (mUIFlipY)
				std::swap(uv0.y, uv1.y);

			float ui_scale = inTexture.mUIScale * mUIScale;
			ImGui::Image((ImTextureID)inTexture.mGPUHandle.ptr, ImVec2(inTexture.mWidth * ui_scale, inTexture.mHeight * ui_scale), uv0, uv1);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				sTexture = &inTexture;
				ImGui::OpenPopup("Image Options");
			}

			ImGui::SameLine();
			std::string text = "-----------------------------------\n"; 
			text += inTexture.mName;
			text += "\n";
			text += std::to_string(inTexture.mWidth) + " x " + std::to_string(inTexture.mHeight) + (inTexture.mDepth == 1 ? "" : " x " + std::to_string(inTexture.mDepth));
			text += "\n";
			text += nameof::nameof_enum(inTexture.mFormat);
			ImGui::Text(text.c_str());
		};

		if (ImGui::BeginPopup("Image Options"))
		{
			if (sTexture != nullptr)
			{
				ImGui::Text(sTexture->mName);

				if (ImGui::Button("Dump"))
					gDumpTexture = sTexture;

				ImGui::Separator();
			}

			ImGui::TextureOption();

			ImGui::EndPopup();
		}

		for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mTextures)
			add_texture(*texture);

		ImGui::TreePop();
	}
}
