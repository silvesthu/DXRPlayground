#include "Cloud.h"
#include "ImGui/imgui_impl_helper.h"

void Cloud::Update()
{
	gCloud.mProfile.mShapeNoise.mOffset += gCloud.mProfile.mWind * ImGui::GetIO().DeltaTime;

	CloudConstants* cloud = static_cast<CloudConstants*>(gCloud.mResource.mConstantUploadBufferPointer);
	cloud->mMode		= gCloud.mProfile.mMode;
	cloud->mRaymarch	= gCloud.mProfile.mRaymarch;
	cloud->mGeometry	= gCloud.mProfile.mGeometry;
	cloud->mShapeNoise	= gCloud.mProfile.mShapeNoise;
}

void Cloud::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(Cloud), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&gCloud.mResource.mConstantUploadBuffer)));
		gCloud.mResource.mConstantUploadBuffer->SetName(L"Cloud.Constant");
		gCloud.mResource.mConstantUploadBuffer->Map(0, nullptr, (void**)&gCloud.mResource.mConstantUploadBufferPointer);
	}

	// Texture
	{
		for (auto&& texture : gCloud.mResource.mTextures)
			texture.Initialize();
	}

	// Shader Binding
	{
		gCloud.mResource.mShapeNoiseShader.InitializeDescriptors({ gCloud.mResource.mShapeNoiseInputTexture.mResource.Get(), gCloud.mResource.mShapeNoiseTexture.mResource.Get() });
		gCloud.mResource.mErosionNoiseShader.InitializeDescriptors({ gCloud.mResource.mErosionNoiseInputTexture.mResource.Get(), gCloud.mResource.mErosionNoiseTexture.mResource.Get() });
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
			gCloud.mResource.mShapeNoiseTexture.Load();
			
			BarrierScope input_resource_scope(gCommandList, gCloud.mResource.mShapeNoiseInputTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierScope resource_scope(gCommandList, gCloud.mResource.mShapeNoiseTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			gCloud.mResource.mShapeNoiseShader.SetupCompute();
			gCommandList->Dispatch(gCloud.mResource.mShapeNoiseTexture.mWidth / 8, gCloud.mResource.mShapeNoiseTexture.mHeight / 8, gCloud.mResource.mShapeNoiseTexture.mDepth);
		}

		{
			gCloud.mResource.mErosionNoiseTexture.Load();

			BarrierScope input_resource_scope(gCommandList, gCloud.mResource.mErosionNoiseInputTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierScope output_resource_scope(gCommandList, gCloud.mResource.mErosionNoiseTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			gCloud.mResource.mErosionNoiseShader.SetupCompute();
			gCommandList->Dispatch(gCloud.mResource.mErosionNoiseTexture.mWidth / 8, gCloud.mResource.mErosionNoiseTexture.mHeight / 8, gCloud.mResource.mErosionNoiseTexture.mDepth);
		}
	}
}

void Cloud::Finalize()
{
	gCloud.mResource.Reset();
}

void Cloud::UpdateImGui()
{
#define SMALL_BUTTON(func) if (ImGui::SmallButton(NAMEOF(func).c_str())) func(gCloud.mProfile);

	if (ImGui::TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (int i = 0; i < static_cast<int>(CloudMode::Count); i++)
		{
			const auto& name = nameof::nameof_enum(static_cast<CloudMode>(i));
			if (i != 0)
				ImGui::SameLine();
			if (ImGui::RadioButton(name.data(), (int)gCloud.mProfile.mMode == i))
				gCloud.mProfile.mMode = static_cast<CloudMode>(i);
		}

		ImGui::SliderFloat3("Wind", &gCloud.mProfile.mWind[0], 0.0f, 100.0f);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Raymarch", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderUint("Sample Count", &gCloud.mProfile.mRaymarch.mSampleCount, 8, 128);
		ImGui::SliderUint("Light Sample Count", &gCloud.mProfile.mRaymarch.mLightSampleCount, 8, 128);
		ImGui::SliderFloat("Light Sample Length (km)", &gCloud.mProfile.mRaymarch.mLightSampleLength, 0.0f, 1.0f);

		SMALL_BUTTON(Profile::RaymarchReference::Default);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderFloat("Strato Bottom (km)", &gCloud.mProfile.mGeometry.mStrato, 0.0f, 8.0f);
		ImGui::SliderFloat("Cirro Bottom (km)", &gCloud.mProfile.mGeometry.mCirro, 0.0f, 8.0f);

		SMALL_BUTTON(Profile::GeometryReference::Schneider15);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Shape Noise", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderFloat3("Offset", &gCloud.mProfile.mShapeNoise.mOffset[0], 0.0f, 100.0f);
		ImGui::NewLine();

		ImGui::SliderFloat("Frequency", &gCloud.mProfile.mShapeNoise.mFrequency, 0.0f, 1.0f);
		ImGui::SliderFloat("Power", &gCloud.mProfile.mShapeNoise.mPower, 0.0f, 100.0f);
		ImGui::SliderFloat("Scale", &gCloud.mProfile.mShapeNoise.mScale, 0.0f, 5.0f);

		SMALL_BUTTON(Profile::ShapeNoiseReference::Default);

		ImGui::TreePop();
	}

	ImGuiShowTextures(gCloud.mResource.mTextures);
}

Cloud gCloud;