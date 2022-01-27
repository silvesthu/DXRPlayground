#include "Cloud.h"

#include "ImGui/imgui_impl_dx12.h"
#include "ImGui/imgui_impl_helper.h"

CloudProfile gCloudProfile;
Cloud gCloud;
CloudResources gCloudResources;

void Cloud::Update()
{
	gCloudProfile.mShapeNoise.mOffset += gCloudProfile.mWind * ImGui::GetIO().DeltaTime;

	CloudConstants* cloud = static_cast<CloudConstants*>(gCloudResources.mConstantUploadBufferPointer);
	cloud->mMode		= gCloudProfile.mMode;
	cloud->mRaymarch	= gCloudProfile.mRaymarch;
	cloud->mGeometry	= gCloudProfile.mGeometry;
	cloud->mShapeNoise	= gCloudProfile.mShapeNoise;
}

void Cloud::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(Cloud), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gCloudResources.mConstantUploadBuffer)));
		gCloudResources.mConstantUploadBuffer->SetName(L"Cloud.Constant");
		gCloudResources.mConstantUploadBuffer->Map(0, nullptr, (void**)&gCloudResources.mConstantUploadBufferPointer);
	}

	// Texture
	{
		for (auto&& texture : gCloudResources.mTextures)
			texture->Initialize();
	}

	// Shader Binding
	{
		gCloudResources.mShapeNoiseShader.InitializeDescriptors({ gCloudResources.mShapeNoiseTexture.mIntermediateResource.Get(), gCloudResources.mShapeNoiseTexture.mResource.Get() });
		gCloudResources.mErosionNoiseShader.InitializeDescriptors({ gCloudResources.mErosionNoiseTexture.mIntermediateResource.Get(), gCloudResources.mErosionNoiseTexture.mResource.Get() });
	}
}

void Cloud::Precompute()
{
	if (!mRecomputeRequested)
		return;
	mRecomputeRequested = false;

	// Load
	{
		{
			gCloudResources.mShapeNoiseTexture.Load();
			
			BarrierScope intermeditate_resource_scope(gCommandList, gCloudResources.mShapeNoiseTexture.mIntermediateResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierScope resource_scope(gCommandList, gCloudResources.mShapeNoiseTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			gCloudResources.mShapeNoiseShader.SetupCompute();
			gCommandList->Dispatch(gCloudResources.mShapeNoiseTexture.mWidth / 8, gCloudResources.mShapeNoiseTexture.mHeight / 8, gCloudResources.mShapeNoiseTexture.mDepth);
		}

		{
			gCloudResources.mErosionNoiseTexture.Load();

			BarrierScope intermeditate_resource_scope(gCommandList, gCloudResources.mErosionNoiseTexture.mIntermediateResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierScope resource_scope(gCommandList, gCloudResources.mErosionNoiseTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			gCloudResources.mErosionNoiseShader.SetupCompute();
			gCommandList->Dispatch(gCloudResources.mErosionNoiseTexture.mWidth / 8, gCloudResources.mErosionNoiseTexture.mHeight / 8, gCloudResources.mErosionNoiseTexture.mDepth);
		}
	}
}

void Cloud::Finalize()
{
	gCloudResources = {};
}

void Cloud::UpdateImGui()
{
#define SMALL_BUTTON(func) if (ImGui::SmallButton(NAMEOF(func).c_str())) func(gCloudProfile);

	if (ImGui::TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < (int)CloudMode::Count; i++)
		{
			const auto& name = nameof::nameof_enum((CloudMode)i);
			if (i != 0)
				ImGui::SameLine();
			if (ImGui::RadioButton(name.data(), (int)gCloudProfile.mMode == i))
				gCloudProfile.mMode = (CloudMode)i;
		}

		ImGui::SliderFloat3("Wind", &gCloudProfile.mWind[0], 0.0f, 100.0f);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Raymarch", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderUint("Sample Count", &gCloudProfile.mRaymarch.mSampleCount, 8, 128);
		ImGui::SliderUint("Light Sample Count", &gCloudProfile.mRaymarch.mLightSampleCount, 8, 128);
		ImGui::SliderFloat("Light Sample Length (km)", &gCloudProfile.mRaymarch.mLightSampleLength, 0.0f, 1.0f);

		SMALL_BUTTON(CloudProfile::RaymarchReference::Default);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderFloat("Strato Bottom (km)", &gCloudProfile.mGeometry.mStrato, 0.0f, 8.0f);
		ImGui::SliderFloat("Cirro Bottom (km)", &gCloudProfile.mGeometry.mCirro, 0.0f, 8.0f);

		SMALL_BUTTON(CloudProfile::GeometryReference::Schneider15);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Shape Noise", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderFloat3("Offset", &gCloudProfile.mShapeNoise.mOffset[0], 0.0f, 100.0f);
		ImGui::NewLine();

		ImGui::SliderFloat("Frequency", &gCloudProfile.mShapeNoise.mFrequency, 0.0f, 1.0f);
		ImGui::SliderFloat("Power", &gCloudProfile.mShapeNoise.mPower, 0.0f, 100.0f);
		ImGui::SliderFloat("Scale", &gCloudProfile.mShapeNoise.mScale, 0.0f, 5.0f);

		SMALL_BUTTON(CloudProfile::ShapeNoiseReference::Default);

		ImGui::TreePop();
	}

	ImGuiShowTextures(gCloudResources.mTextures);
}
