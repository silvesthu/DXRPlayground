#include "Common.h"
#include "Cloud.h"

#include "ImGui/imgui_impl_dx12.h"
#include "ImGui/imgui_impl_helper.h"

CloudProfile gCloudProfile;
Cloud gCloud;
CloudResources gCloudResources;

void Cloud::Update()
{
	ShaderType::Cloud* cloud = static_cast<ShaderType::Cloud*>(gCloudResources.mConstantUploadBufferPointer);

	cloud->mMode = static_cast<glm::uint32>(gCloudProfile.mMode);
}


void Cloud::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(ShaderType::Cloud), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gCloudResources.mConstantUploadBuffer)));
		gCloudResources.mConstantUploadBuffer->SetName(L"Cloud.Constant");
		gCloudResources.mConstantUploadBuffer->Map(0, nullptr, (void**)&gCloudResources.mConstantUploadBufferPointer);
	}
}

void Cloud::Finalize()
{
	gCloudResources = {};
}

void Cloud::UpdateImGui()
{
#define SMALL_BUTTON(func) if (ImGui::SmallButton(NAMEOF(func).c_str())) func(gCloudProfile);

	if (ImGui::TreeNodeEx("Mode", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < (int)CloudMode::Count; i++)
		{
			const auto& name = nameof::nameof_enum((CloudMode)i);
			if (i != 0)
				ImGui::SameLine();
			if (ImGui::RadioButton(name.data(), (int)gCloudProfile.mMode == i))
				gCloudProfile.mMode = (CloudMode)i;
		}

		ImGui::TreePop();
	}
}
