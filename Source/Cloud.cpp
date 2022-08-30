#include "Cloud.h"
#include "ImGui/imgui_impl_helper.h"

void Cloud::Update()
{
	mProfile.mShapeNoise.mOffset += mProfile.mWind * ImGui::GetIO().DeltaTime;

	CloudConstants* cloud = static_cast<CloudConstants*>(mRuntime.mConstantUploadBufferPointer);
	cloud->mMode		= mProfile.mMode;
	cloud->mRaymarch	= mProfile.mRaymarch;
	cloud->mGeometry	= mProfile.mGeometry;
	cloud->mShapeNoise	= mProfile.mShapeNoise;
}

void Cloud::Initialize()
{
	// Buffer
	{
		D3D12_RESOURCE_DESC desc = gGetBufferResourceDesc(gAlignUp((UINT)sizeof(Cloud), (UINT)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		D3D12_HEAP_PROPERTIES props = gGetUploadHeapProperties();

		gValidate(gDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mRuntime.mConstantUploadBuffer)));
		mRuntime.mConstantUploadBuffer->SetName(L"Cloud.Constant");
		mRuntime.mConstantUploadBuffer->Map(0, nullptr, (void**)&mRuntime.mConstantUploadBufferPointer);
	}

	// Texture
	{
		for (auto&& texture : mRuntime.mTextures)
			texture.Initialize();
	}

	// Shader Binding
	{
		mRuntime.mShapeNoiseShader.InitializeDescriptors({ mRuntime.mShapeNoiseInputTexture.mResource.Get(), mRuntime.mShapeNoiseTexture.mResource.Get() });
		mRuntime.mErosionNoiseShader.InitializeDescriptors({ mRuntime.mErosionNoiseInputTexture.mResource.Get(), mRuntime.mErosionNoiseTexture.mResource.Get() });
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
			mRuntime.mShapeNoiseTexture.Update();
			
			BarrierScope input_resource_scope(gCommandList, mRuntime.mShapeNoiseInputTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierScope resource_scope(gCommandList, mRuntime.mShapeNoiseTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			mRuntime.mShapeNoiseShader.SetupCompute();
			gCommandList->Dispatch(mRuntime.mShapeNoiseTexture.mWidth / 8, mRuntime.mShapeNoiseTexture.mHeight / 8, mRuntime.mShapeNoiseTexture.mDepth);
		}

		{
			mRuntime.mErosionNoiseTexture.Update();

			BarrierScope input_resource_scope(gCommandList, mRuntime.mErosionNoiseInputTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierScope output_resource_scope(gCommandList, mRuntime.mErosionNoiseTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			mRuntime.mErosionNoiseShader.SetupCompute();
			gCommandList->Dispatch(mRuntime.mErosionNoiseTexture.mWidth / 8, mRuntime.mErosionNoiseTexture.mHeight / 8, mRuntime.mErosionNoiseTexture.mDepth);
		}
	}
}

void Cloud::Finalize()
{
	mRuntime.Reset();
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

	ImGuiShowTextures(mRuntime.mTextures);
}

Cloud gCloud;