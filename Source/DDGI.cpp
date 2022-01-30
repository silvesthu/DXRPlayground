#include "DDGI.h"
#include "ImGui/imgui_impl_helper.h"

void DDGI::Update()
{
}

void DDGI::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(DDGI), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gDDGI.mResource.mConstantUploadBuffer)));
		gDDGI.mResource.mConstantUploadBuffer->SetName(L"DDGI.Constant");
		gDDGI.mResource.mConstantUploadBuffer->Map(0, nullptr, (void**)&gDDGI.mResource.mConstantUploadBufferPointer);
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

					Probe probe;
					probe.mPosition = glm::vec4(x, y, z, 1);
					gDDGI.mResource.mProbes.push_back(probe);
				}
		
		for (auto&& probe : gDDGI.mResource.mProbes)
		{
			probe.mIrradianceTexture.Initialize();
			probe.mDistanceTexture.Initialize();
		}
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
	gDDGI.mResource.Reset();
}

void DDGI::UpdateImGui()
{
	int index = 0;
	for (auto&& probe : gDDGI.mResource.mProbes)
		ImGuiShowTextures({ &probe.mIrradianceTexture, &probe.mDistanceTexture }, std::string("Probe ") + std::to_string(index++));
}

DDGI gDDGI;