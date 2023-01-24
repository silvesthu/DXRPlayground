#pragma once
#include "Common.h"

// [Schneider15] The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn
// [Schneider16] GPU Pro 7 Advanced Rendering Techniques, Real-Time Volumetric Cloudscapes

class Cloud
{
public:
	struct Profile
	{
		CloudMode mMode = CloudMode::None;
		glm::vec3 mWind = glm::vec3(0.0f, 0.0f, 0.0f);

		struct RaymarchReference
		{
			static void Default(Profile& profile)
			{
				profile.mRaymarch.mSampleCount = 80;
				profile.mRaymarch.mLightSampleCount = 16;
				profile.mRaymarch.mLightSampleLength = 0.01f; // km
			}
		};
		CloudConstants::RayMarch mRaymarch;

		struct GeometryReference
		{
			static void Schneider15(Profile& profile)
			{
				// page 73
				profile.mGeometry.mStrato = 1.5; // km
				profile.mGeometry.mCirro = 4.0; // km
			}
		};
		CloudConstants::Geometry mGeometry;

		struct ShapeNoiseReference
		{
			static void Default(Profile& profile)
			{
				profile.mShapeNoise.mFrequency = 0.02f;
				profile.mShapeNoise.mPower = 60.0f;
				profile.mShapeNoise.mScale = 1.0f;
			}
		};
		CloudConstants::ShapeNoise mShapeNoise;

		Profile()
		{
			RaymarchReference::Default(*this);
			GeometryReference::Schneider15(*this);
			ShapeNoiseReference::Default(*this);
		}
	};
	Profile mProfile;

	struct Runtime : RuntimeBase<Runtime>
	{
		// Shaders
		Shader mShapeNoiseShader				= Shader().FileName("Shader/Cloud.hlsl").CSName("CloudShapeNoiseCS");
		Shader mErosionNoiseShader				= Shader().FileName("Shader/Cloud.hlsl").CSName("CloudErosionNoiseCS");
		// Gather shaders
		Shader mSentinelShader					= Shader();
		std::span<Shader> mShaders				= std::span<Shader>(&mShapeNoiseShader, &mSentinelShader);
		
		// Textures
		Texture mShapeNoise3DTexture			= Texture().Width(128).Height(128).Depth(128).Format(DXGI_FORMAT_R8G8B8A8_UNORM).
															UAVIndex(ViewDescriptorIndex::CloudShapeNoise3DUAV).SRVIndex(ViewDescriptorIndex::CloudShapeNoise3DSRV).SRVFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).Name("Cloud.ShapeNoise");
		Texture mErosionNoise3DTexture			= Texture().Width(32).Height(32).Depth(32).Format(DXGI_FORMAT_R8G8B8A8_UNORM).
															UAVIndex(ViewDescriptorIndex::CloudErosionNoise3DUAV).SRVIndex(ViewDescriptorIndex::CloudErosionNoise3DSRV).SRVFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB).Name("Cloud.ErosionNoise");

		Texture mShapeNoise2DTexture			= Texture().Width(16384).Height(128).Format(DXGI_FORMAT_R8G8B8A8_UNORM).
															UAVIndex(ViewDescriptorIndex::CloudShapeNoise2DUAV).SRVIndex(ViewDescriptorIndex::CloudShapeNoise2DSRV).Name("Cloud.ShapeNoise.Input").Path(L"Asset/TileableVolumeNoise/noiseShapePacked.tga");
		Texture mErosionNoise2DTexture			= Texture().Width(1024).Height(32).Format(DXGI_FORMAT_R8G8B8A8_UNORM).
															UAVIndex(ViewDescriptorIndex::CloudErosionNoise2DUAV).SRVIndex(ViewDescriptorIndex::CloudErosionNoise2DSRV).Name("Cloud.ErosionNoise.Input").Path(L"Asset/TileableVolumeNoise/noiseErosionPacked.tga");
		
		Texture mSentinelTexture				= Texture();
		std::span<Texture> mTextures			= std::span<Texture>(&mShapeNoise3DTexture, &mSentinelTexture);
	};
	Runtime mRuntime;

	void Initialize();
	void Finalize();
	void Update();
	void Render();
	void ImGuiShowMenus();
	void ImGuiShowTextures();

	bool mRecomputeRequested = true;
};
extern Cloud gCloud;