#include "Common.h"
#include "Atmosphere.h"

#include "ImGui/imgui_impl_dx12.h"

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

	atmosphere->mSolarIrradiance					= glm::vec3(1.474000f, 1.850400f, 1.911980f);
	atmosphere->mSunAngularRadius					= 0.004675f;

	atmosphere->mUnifyXYEncode						= mUnifyXYEncode ? 1 : 0;

	// Density Profile: { Width, ExpTerm, ExpScale, LinearTerm, ConstantTerm }
	{
		// Rayleigh
		{
			switch (gAtmosphereProfile.mRayleighMode)
			{
			case AtmosphereProfile::RayleighMode::BN08Impl:
				gAtmosphereProfile.mRayleighScatteringCoefficient = gAtmosphereProfile.mRayleighScatteringCoefficient_;
				break;
			case AtmosphereProfile::RayleighMode::BN08:
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

				// Total scattering coefficient = gAtmosphereProfile.mRayleighScatteringCoefficient = integral of angular scattering coefficient in all directions
				// Angular scattering coefficient = Total scattering coefficient * (1 + cos(theta)^2) * 3.0 / 2.0
			}
			break;
			default:
				break;
			}

			// Coefficient
			atmosphere->mRayleighScattering = UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mRayleighScatteringCoefficient);

			// Extinction Coefficient
			// Air molecules do not absorb light

			// Density: decrease exponentially
			atmosphere->mRayleighDensity.mLayer0 = { dummy, dummy, dummy, dummy, dummy };
			atmosphere->mRayleighDensity.mLayer1 = { dummy, 1.0f, -1.0f / UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kRayleighScaleHeight), 0.0f, 0.0f };
		}

		// Mie
		{
			// [TODO] mie scattering 0.003996,0.003996,0.003996

			switch (gAtmosphereProfile.mMieMode)
			{
			case AtmosphereProfile::MieMode::BN08Impl:
			{
				// [TODO] Further reference?
				gAtmosphereProfile.mMieExtinctionCoefficient = gAtmosphereProfile.kMieAngstromBeta / gAtmosphereProfile.kMieScaleHeight * glm::pow(gAtmosphereProfile.kLambda, glm::dvec3(-gAtmosphereProfile.kMieAngstromAlpha));

				// [TODO] Why this is different from the paper
				gAtmosphereProfile.mMieScatteringCoefficient = gAtmosphereProfile.mMieExtinctionCoefficient * gAtmosphereProfile.kMieSingleScatteringAlbedo;
			}
			break;
			case AtmosphereProfile::MieMode::BN08:
			{
				gAtmosphereProfile.mMieExtinctionCoefficient = gAtmosphereProfile.mMieExtinctionCoefficientPaper;
				gAtmosphereProfile.mMieScatteringCoefficient = gAtmosphereProfile.mMieScatteringCoefficientPaper;
			}
			break;
			default:
				break;
			}

			// Coefficient
			atmosphere->mMieExtinction = UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mMieExtinctionCoefficient);
			atmosphere->mMieScattering = UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mMieScatteringCoefficient);

			// Density: decrease exponentially
			atmosphere->mMieDensity.mLayer0 = { dummy, dummy, dummy, dummy, dummy };
			atmosphere->mMieDensity.mLayer1 = { dummy, 1.0f, -1.0f / UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kRayleighScaleHeight), 0.0f, 0.0f };
		}

		// Ozone
		{
			// [TODO] Further reference [BN08]
			atmosphere->mAbsorptionExtinction = glm::vec4(0.000650f, 0.001881f, 0.000085f, 0.0f);

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

			if (gAtmosphereProfile.kEnableOzone)
			{
				atmosphere->mAbsorptionDensity.mLayer0 = { ozone_bottom_altitude, 0.0f, 0.0f, layer_0_linear_term, layer_0_constant_term };
				atmosphere->mAbsorptionDensity.mLayer1 = { dummy, 0.0f, 0.0f, layer_1_linear_term, layer_1_constant_term };
			}
			else
			{
				atmosphere->mAbsorptionDensity.mLayer0 = { dummy, 0.0f, 0.0f, 0.0f, 0.0f };
				atmosphere->mAbsorptionDensity.mLayer1 = { dummy, 0.0f, 0.0f, 0.0f, 0.0f };
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
	gPrecomputedAtmosphereScatteringResources.mComputeTransmittanceShader.Setup();
	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture.mHeight / 8, 1);
}

void PrecomputedAtmosphereScattering::ComputeDirectIrradiance()
{
	gPrecomputedAtmosphereScatteringResources.mComputeDirectIrradianceShader.Setup();
	gCommandList->Dispatch(gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mWidth / 8, gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mHeight / 8, 1);
}

void PrecomputedAtmosphereScattering::ComputeSingleScattering()
{

}

void PrecomputedAtmosphereScattering::ComputeScatteringDensity()
{

}

void PrecomputedAtmosphereScattering::ComputeIndirectIrradiance()
{

}

void PrecomputedAtmosphereScattering::AccumulateMultipleScattering()
{

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
		std::vector<Shader::DescriptorEntry> common_binding =
		{
			// CBV
			gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->GetGPUVirtualAddress(),

			// UAV
			gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture.mResource.Get(),
			gPrecomputedAtmosphereScatteringResources.mDeltaIrradianceTexture.mResource.Get(),
			gPrecomputedAtmosphereScatteringResources.mIrradianceTexture.mResource.Get(),

			// SRV
			gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture.mResource.Get(),
		};

		for (auto&& shader : gPrecomputedAtmosphereScatteringResources.mShaders)
			shader->Initialize(common_binding);
	}
}

void PrecomputedAtmosphereScattering::Finalize()
{
	gPrecomputedAtmosphereScatteringResources = {};
}

void PrecomputedAtmosphereScattering::UpdateImGui()
{
	if (ImGui::TreeNodeEx("Profile"))
	{
		glm::f64 earth_radius_min = 1000.0;
		glm::f64 earth_radius_max = 10000000.0;
		ImGui::SliderScalar("Earth Radius (m)", ImGuiDataType_Double, &gAtmosphereProfile.kBottomRadius, &earth_radius_min, &earth_radius_max);
		glm::f64 atmosphere_thickness_min = 1000.0;
		glm::f64 atmosphere_thickness_max = 100000.0;
		ImGui::SliderScalar("Atmosphere Thickness (m)", ImGuiDataType_Double, &gAtmosphereProfile.kAtmosphereThickness, &atmosphere_thickness_min, &atmosphere_thickness_max);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::TreeNodeEx("Rayleigh", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec3 scattering_coefficient = gAtmosphereProfile.mRayleighScatteringCoefficient * 1e6;
			ImGui::InputFloat3("Scattering 1e-6", &scattering_coefficient.x);
			ImGui::RadioButton("BN08Impl", (int*)&gAtmosphereProfile.mRayleighMode, (int)AtmosphereProfile::RayleighMode::BN08Impl);
			ImGui::SameLine();
			ImGui::RadioButton("BN08", (int*)&gAtmosphereProfile.mRayleighMode, (int)AtmosphereProfile::RayleighMode::BN08);
			ImGui::SameLine();
			ImGui::RadioButton("PSS99", (int*)&gAtmosphereProfile.mRayleighMode, (int)AtmosphereProfile::RayleighMode::PSS99);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Mie", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec3 extinction_coefficient = gAtmosphereProfile.mMieExtinctionCoefficient * 1e6;
			ImGui::InputFloat3("Extinction 1e-6 (/m)", &extinction_coefficient.x);
			glm::vec3 scattering_coefficient = gAtmosphereProfile.mMieScatteringCoefficient * 1e6;
			ImGui::InputFloat3("Scattering 1e-6 (/m)", &scattering_coefficient.x);
			ImGui::RadioButton("BN08Impl", (int*)&gAtmosphereProfile.mMieMode, (int)AtmosphereProfile::MieMode::BN08Impl);
			ImGui::SameLine();
			ImGui::RadioButton("BN08", (int*)&gAtmosphereProfile.mMieMode, (int)AtmosphereProfile::MieMode::BN08);

			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Ozone", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Enable", &gAtmosphereProfile.kEnableOzone);

			ImGui::TreePop();
		}

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::PushItemWidth(100);
		ImGui::SliderFloat("UI Scale", &mUIScale, 1.0, 4.0f);
		ImGui::PopItemWidth();
		ImGui::SameLine();
		ImGui::Checkbox("UI Flip Y", &mUIFlipY);

		ImGui::Checkbox("Unify XY Encode", &mUnifyXYEncode);

		auto add_texture = [&](Texture& inTexture)
		{
			ImVec2 uv0 = ImVec2(0, 0);
			ImVec2 uv1 = ImVec2(1, 1);
			
			if (mUIFlipY)
				std::swap(uv0.y, uv1.y);

			ImGui::Image((ImTextureID)inTexture.mGPUHandle.ptr,
				ImVec2(inTexture.mWidth * inTexture.mUIScale * mUIScale, inTexture.mHeight * inTexture.mUIScale * mUIScale), uv0, uv1);
			ImGui::SameLine();
			std::string text = inTexture.mName;
			text += "\n";
			text += std::to_string(inTexture.mWidth) + "x" + std::to_string(inTexture.mHeight);
			ImGui::Text(text.c_str());
		};

		for (auto&& texture : gPrecomputedAtmosphereScatteringResources.mTextures)
			add_texture(*texture);

		ImGui::TreePop();
	}
}
