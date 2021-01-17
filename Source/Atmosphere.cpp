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

void PrecomputedAtmosphereScatteringResources::Update()
{
	ShaderType::Atmosphere* atmosphere = static_cast<ShaderType::Atmosphere*>(mConstantUploadBufferPointer);

	auto inv_m_to_inv_km							= [](glm::f64 inInvM) { return static_cast<float>(inInvM * 1000.0); };

	atmosphere->mBottomRadius						= UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.BottomRadius());
	atmosphere->mTopRadius							= UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.TopRadius());

	// Density Profile: { Width, ExpTerm, ExpScale, LinearTerm, ConstantTerm }

	float dummy = 0.0f;

	// Rayleigh
	{
		// Wavelength -> Scattering Coeffient
		if (gAtmosphereProfile.kWavelengthToCoefficient)
		{
			// Calculate kRayleigh
			if (gAtmosphereProfile.kUsePSS99)
			{
				// [PSS99] A.3
				constexpr double pi = glm::pi<double>();
				double n = 1.0003; // index of refraction of air
				double N = 2.545e25; // number of molecules per unit volume
				double p_n = 0.035; // depolarization factor
				gAtmosphereProfile.kRayleigh = 
					( ( 8.0 * glm::pow(pi, 3.0) * glm::pow((n *  n - 1.0), 2.0) ) * ( 6.0 + 3.0 * p_n ) )
					/ // -----------------------------------------------------------------------------------------
					( ( 3.0 * N ) * (6.0 - 7.0 * p_n) );

				// Total scattering coefficient = gAtmosphereProfile.mRayleighScatteringCoefficient = integral of angular scattering coefficient in all directions
				// Angular scattering coefficient = Total scattering coefficient * (1 + cos(theta)^2) * 3.0 / 2.0
			}

			// Scattering Coefficient
			gAtmosphereProfile.mRayleighScatteringCoefficient = gAtmosphereProfile.kRayleigh / glm::pow(UnitHelper::sNanometerToMeter<glm::dvec3>(gAtmosphereProfile.kLambda), glm::dvec3(4.0));
		}

		// Scattering Coefficient
		atmosphere->mRayleighScattering				= UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mRayleighScatteringCoefficient);

		// Extinction Coefficient
		// Air molecules do not absorb light

		// Density: decrease exponentially
		atmosphere->mRayleighDensity.mLayer0		= { dummy, dummy, dummy, dummy, dummy };
		atmosphere->mRayleighDensity.mLayer1		= { dummy, 1.0f, -1.0f / UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kRayleighScaleHeight), 0.0f, 0.0f };
	}

	// Mie
	{
		// [TODO] mie scattering 0.003996,0.003996,0.003996

		if (gAtmosphereProfile.kWavelengthToCoefficient)
		{
			// [TODO] Further reference? Micrometer? Why different from [BN08]
			gAtmosphereProfile.mMieExtinctionCoefficient = gAtmosphereProfile.kMieAngstromBeta / gAtmosphereProfile.kMieScaleHeight * glm::pow(gAtmosphereProfile.kLambda, glm::dvec3(-gAtmosphereProfile.kMieAngstromAlpha));
			gAtmosphereProfile.mMieScatteringCoefficient = gAtmosphereProfile.mMieExtinctionCoefficient * gAtmosphereProfile.kMieSingleScatteringAlbedo;
		}

		// Scattering Coefficient
		atmosphere->mMieScattering					= UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mMieScatteringCoefficient);

		// Extinction Coefficient
		atmosphere->mMieExtinction					= UnitHelper::sInverseMeterToInverseKilometer<glm::vec3>(gAtmosphereProfile.mMieExtinctionCoefficient);

		// Density: decrease exponentially
		atmosphere->mMieDensity.mLayer0				= { dummy, dummy, dummy, dummy, dummy };
		atmosphere->mMieDensity.mLayer1				= { dummy, 1.0f, -1.0f / UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kRayleighScaleHeight), 0.0f, 0.0f };
	}

	// Ozone
	{
		atmosphere->mAbsorptionExtinction			= glm::vec4(0.000650f, 0.001881f, 0.000085f, 0.0f);

		// Density: increase linearly, then decrease linearly
		float ozone_bottom_altitude					= UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kOzoneBottomAltitude);
		float ozone_mid_altitude					= UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kOzoneMidAltitude);
		float ozone_top_altitude					= UnitHelper::stMeterToKilometer<float>(gAtmosphereProfile.kOzoneTopAltitude);
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

		if (gAtmosphereProfile.kUseOzone)
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
}

void PrecomputedAtmosphereScattering::Initialize()
{
	// Texture
	{
		D3D12_RESOURCE_DESC resource_desc = gGetTextureResourceDesc(256, 64, DXGI_FORMAT_R32G32B32A32_FLOAT);
		D3D12_HEAP_PROPERTIES props = gGetDefaultHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture)));
		gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture->SetName(L"Atmosphere.Transmittance");

		// SRV for ImGui
		ImGui_ImplDX12_AllocateDescriptor(gPrecomputedAtmosphereScatteringResources.mTransmittanceTextureCPUHandle, gPrecomputedAtmosphereScatteringResources.mTransmittanceTextureGPUHandle);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = (UINT)-1;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		gDevice->CreateShaderResourceView(gPrecomputedAtmosphereScatteringResources.mTransmittanceTexture.Get(), &srv_desc, gPrecomputedAtmosphereScatteringResources.mTransmittanceTextureCPUHandle);
	}

	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(ShaderType::Atmosphere), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer)));
		gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->SetName(L"Atmosphere.Constant");
		gPrecomputedAtmosphereScatteringResources.mConstantUploadBuffer->Map(0, nullptr, (void**)&gPrecomputedAtmosphereScatteringResources.mConstantUploadBufferPointer);
	}
}
