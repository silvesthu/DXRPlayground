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
				profile.mGeometry.mAlto = 4.0; // km
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

	struct Resource : ResourceBase<Resource>
	{
		ComPtr<ID3D12Resource> mConstantUploadBuffer;
		void* mConstantUploadBufferPointer		= nullptr;

		// Shaders
		Shader mShapeNoiseShader				= Shader().FileName("Shader/Cloud.hlsl").CSName("CloudShapeNoiseCS");
		Shader mErosionNoiseShader				= Shader().FileName("Shader/Cloud.hlsl").CSName("CloudErosionNoiseCS");
		// Gather shaders
		Shader mSentinelShader					= Shader();
		std::span<Shader> mShaders				= std::span<Shader>(&mShapeNoiseShader, &mSentinelShader);
		
		// Textures
		Texture mShapeNoiseTexture				= Texture().Width(128).Height(128).Depth(128).Format(DXGI_FORMAT_R8_UNORM).Name("Cloud.ShapeNoise").UIScale(2.0f);
		Texture mErosionNoiseTexture			= Texture().Width(32).Height(32).Depth(32).Format(DXGI_FORMAT_R8_UNORM).Name("Cloud.ErosionNoise").UIScale(8.0f);
		Texture mShapeNoiseInputTexture			= Texture().Width(128).Height(128).Depth(128).Format(DXGI_FORMAT_R8_UNORM).Name("Cloud.ShapeNoise.Input").UIScale(2.0f).Path(L"Asset/TileableVolumeNoise/noiseShapePacked.tga");
		Texture mErosionNoiseInputTexture		= Texture().Width(32).Height(32).Depth(32).Format(DXGI_FORMAT_R8_UNORM).Name("Cloud.ErosionNoise.Input").UIScale(8.0f).Path(L"Asset/TileableVolumeNoise/noiseErosionPacked.tga");
		// Gather textures
		Texture mSentinelTexture				= Texture();
		std::span<Texture> mTextures			= std::span<Texture>(&mShapeNoiseTexture, &mSentinelTexture);
	};
	Resource mResource;

	void Initialize();
	void Precompute();
	void Finalize();
	void UpdateImGui();
	void Update();

	bool mRecomputeRequested = true;
};
extern Cloud gCloud;