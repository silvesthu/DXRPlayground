#include "Common.h"
#include "Atmosphere.h"

#include "ImGui/imgui_impl_dx12.h"

AtmosphereProfile gAtmosphereProfile;
PrecomputedAtmosphereScattering gPrecomputedAtmosphereScattering;
PrecomputedAtmosphereScatteringResources gPrecomputedAtmosphereScatteringResources;

void PrecomputedAtmosphereScatteringResources::Update()
{
	ShaderType::Atmosphere* atmosphere = static_cast<ShaderType::Atmosphere*>(mConstantUploadBufferPointer);

	auto m_to_km = [](glm::f64 inM) { return static_cast<float>(inM / 1000.0); };

	atmosphere->mBottomRadius = m_to_km(gAtmosphereProfile.AtmosphereBottomRadius());
	atmosphere->mTopRadius = m_to_km(gAtmosphereProfile.AtmosphereTopRadius());
	// gAtmosphereProfile
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
