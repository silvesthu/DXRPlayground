#include "DDGI.h"

#include "ImGui/imgui_impl_dx12.h"
#include "ImGui/imgui_impl_helper.h"

DDGI gDDGI;
DDGIResources gDDGIResources;

void DDGI::Update()
{
	// ShaderType::DDGI* ddgi = static_cast<ShaderType::DDGI*>(gDDGIResources.mConstantUploadBufferPointer);
}

void DDGI::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(ShaderType::DDGI), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gDDGIResources.mConstantUploadBuffer)));
		gDDGIResources.mConstantUploadBuffer->SetName(L"DDGI.Constant");
		gDDGIResources.mConstantUploadBuffer->Map(0, nullptr, (void**)&gDDGIResources.mConstantUploadBufferPointer);
	}

	// Texture
	{
		// int count = 3;
		int count = 1;

		float size = 0.75f;
		
		if (count == 1)
			size = 0;

		for (int i = 0; i < count; i++)
			for (int j = 0; j < count; j++)
				for (int k = 0; k < count; k++)
				{
					float x = -size + k * (2.0f * size) / count;
					float y = -size + j * (2.0f * size) / count;
					float z = -size + i * (2.0f * size) / count;

					DDGIProbe probe;
					probe.mPosition = glm::vec4(x, y, z, 1);
					gDDGIResources.mProbes.push_back(probe);
				}

		for (auto&& probe : gDDGIResources.mProbes)
		{
			probe.mIrradianceTexture.Initialize();
			probe.mDistanceTexture.Initialize();
		}

		gDDGIResources.mDummyProbe.mIrradianceTexture.Initialize();
		gDDGIResources.mDummyProbe.mDistanceTexture.Initialize();
	}

	// Shader Binding
	{
		gDDGIResources.mPrecomputeShader.InitializeDescriptors({ gDDGIResources.mDummyProbe.mIrradianceTexture.mResource.Get(), gDDGIResources.mDummyProbe.mDistanceTexture.mResource.Get() });
	}
}

void DDGI::Precompute()
{
	if (!mRecomputeRequested)
		return;
	mRecomputeRequested = false;
}

void DDGI::Finalize()
{
	gDDGIResources = {};
}

void DDGI::UpdateImGui()
{
	int index = 0;
	for (auto&& probe : gDDGIResources.mProbes)
		ImGuiShowTextures({ &probe.mIrradianceTexture, &probe.mDistanceTexture }, std::string("Probe ") + std::to_string(index++));
}
