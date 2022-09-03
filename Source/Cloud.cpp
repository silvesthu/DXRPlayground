#include "Cloud.h"
#include "ImGui/imgui_impl_helper.h"

void Cloud::Update()
{
	mProfile.mShapeNoise.mOffset += mProfile.mWind * ImGui::GetIO().DeltaTime;

	CloudConstants& constants	= gConstants.mCloud;
	constants.mMode				= mProfile.mMode;
	constants.mRaymarch			= mProfile.mRaymarch;
	constants.mGeometry			= mProfile.mGeometry;
	constants.mShapeNoise		= mProfile.mShapeNoise;

	// Texture
	for (auto&& texture : mRuntime.mTextures)
		texture.Update();
}

void Cloud::Initialize()
{
	// Texture
	for (auto&& texture : mRuntime.mTextures)
		texture.Initialize();
}

void Cloud::Precompute()
{
	if (!mRecomputeRequested)
		return;
	mRecomputeRequested = false;

	// Load
	{
		{
			mRuntime.mShapeNoise3DTexture.Update();
			
			BarrierScope input_resource_scope(gCommandList, mRuntime.mShapeNoise2DTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierScope resource_scope(gCommandList, mRuntime.mShapeNoise3DTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			mRuntime.mShapeNoiseShader.SetupCompute(nullptr, true);
			gCommandList->Dispatch(mRuntime.mShapeNoise3DTexture.mWidth / 8, mRuntime.mShapeNoise3DTexture.mHeight / 8, mRuntime.mShapeNoise3DTexture.mDepth);
		}

		{
			mRuntime.mErosionNoise3DTexture.Update();

			BarrierScope input_resource_scope(gCommandList, mRuntime.mErosionNoise2DTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			BarrierScope output_resource_scope(gCommandList, mRuntime.mErosionNoise3DTexture.mResource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			mRuntime.mErosionNoiseShader.SetupCompute(nullptr, true);
			gCommandList->Dispatch(mRuntime.mErosionNoise3DTexture.mWidth / 8, mRuntime.mErosionNoise3DTexture.mHeight / 8, mRuntime.mErosionNoise3DTexture.mDepth);
		}
	}
}

void Cloud::Finalize()
{
	mRuntime.Reset();
}

void Cloud::ImGuiShowMenus()
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
}

void Cloud::ImGuiShowTextures()
{
	ImGui::Textures(mRuntime.mTextures, "Cloud", ImGuiTreeNodeFlags_None);
}

Cloud gCloud;