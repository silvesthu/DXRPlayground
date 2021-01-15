#include "Common.h"
#include "Atmosphere.h"

#include "ImGui/imgui_impl_dx12.h"

AtmosphereProfile gAtmosphereProfile;
PrecomputedAtmosphereScattering gPrecomputedAtmosphereScattering;
PrecomputedAtmosphereScatteringResources gPrecomputedAtmosphereScatteringResources;

void PrecomputedAtmosphereScatteringResources::Update()
{
	ShaderType::Atmosphere* atmosphere = static_cast<ShaderType::Atmosphere*>(mConstantUploadBufferPointer);

	auto m_to_km									= [](glm::f64 inM) { return static_cast<float>(inM / 1000.0); };

	atmosphere->mBottomRadius						= m_to_km(gAtmosphereProfile.BottomRadius());
	atmosphere->mTopRadius							= m_to_km(gAtmosphereProfile.TopRadius());

	// Density Profile: { Width, ExpTerm, ExpScale, LinearTerm, ConstantTerm }

	float dummy = 0.0f;

	// rayleigh - density decrease exponentially
	atmosphere->mRayleighScattering					= glm::vec4(0.005802f, 0.013558f, 0.033100f, dummy);
	atmosphere->mRayleighDensity.mLayer0			= { dummy, dummy, dummy, dummy, dummy };
	atmosphere->mRayleighDensity.mLayer1			= { dummy, 1.0f, -1.0f / m_to_km(gAtmosphereProfile.kRayleighScaleHeight), 0.0f, 0.0f };
	
	// mie - density decrease exponentially
	// [TODO] mie scattering 0.003996,0.003996,0.003996
	atmosphere->mMieExtinction						= glm::vec4(0.004440f, 0.004440f, 0.004440f, 0.0f);
	atmosphere->mMieDensity.mLayer0					= { dummy, dummy, dummy, dummy, dummy };
	atmosphere->mMieDensity.mLayer1					= { dummy, 1.0f, -1.0f / m_to_km(gAtmosphereProfile.kRayleighScaleHeight), 0.0f, 0.0f };

	// ozone - density increase linearly, then decrease linearly
	auto calculate_linear_term = [](float inX0, float inX1, float& outLinearTerm, float& outConstantTerm)
	{
		outLinearTerm = 1.0f / (inX1 - inX0);
		outConstantTerm = 1.0f * (0.0f - inX0) / (inX1 - inX0);
	};
	float ozone_bottom_height						= m_to_km(gAtmosphereProfile.kOZoneBottomHeight);
	float ozone_mid_height							= m_to_km(gAtmosphereProfile.kOZoneMidHeight);
	float ozone_top_height							= m_to_km(gAtmosphereProfile.kOZoneTopHeight);
	float layer_0_linear_term, layer_0_constant_term, layer_1_linear_term, layer_1_constant_term;
	calculate_linear_term(ozone_bottom_height, ozone_mid_height, layer_0_linear_term, layer_0_constant_term);
	calculate_linear_term(ozone_top_height, ozone_mid_height, layer_1_linear_term, layer_1_constant_term);

	atmosphere->mAbsorptionExtinction				= glm::vec4(0.000650f, 0.001881f, 0.000085f, 0.0f);
	atmosphere->mAbsorptionDensity.mLayer0			= { ozone_bottom_height, 0.0f, 0.0f, layer_0_linear_term, layer_0_constant_term };
	atmosphere->mAbsorptionDensity.mLayer1			= { dummy, 0.0f, 0.0f, layer_1_linear_term, layer_1_constant_term };
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
