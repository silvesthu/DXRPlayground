#pragma once

#include "Common.h"

// [Schneider15] The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn
// [Schneider16] GPU Pro 7 Advanced Rendering Techniques, Real-Time Volumetric Cloudscapes

struct CloudProfile
{
	CloudMode mMode = CloudMode::None;
	glm::vec3 mWind = glm::vec3(0.0f, 0.0f, 0.0f);

	struct RaymarchReference
	{
		static void Default(CloudProfile& profile)
		{
			profile.mRaymarch.mSampleCount = 80;
			profile.mRaymarch.mLightSampleCount = 16;
			profile.mRaymarch.mLightSampleLength = 0.01f; // km
		}
	};
	ShaderType::CloudRaymarch mRaymarch;

	struct GeometryReference
	{
		static void Schneider15(CloudProfile& profile)
		{
			// page 73
			profile.mGeometry.mStrato = 1.5; // km
			profile.mGeometry.mAlto = 4.0; // km
			profile.mGeometry.mCirro = 4.0; // km
		}
	};
	ShaderType::CloudGeometry mGeometry;

	struct ShapeNoiseReference
	{
		static void Default(CloudProfile& profile)
		{
			profile.mShapeNoise.mFrequency = 0.02f;
			profile.mShapeNoise.mPower = 60.0f;
			profile.mShapeNoise.mScale = 1.0f;
		}
	};
	ShaderType::CloudShapeNoise mShapeNoise;

	CloudProfile()
	{
		RaymarchReference::Default(*this);
		GeometryReference::Schneider15(*this);
		ShapeNoiseReference::Default(*this);
	}
};

class Cloud
{
public:
	void Initialize();
	void Precompute();
	void Finalize();
	void UpdateImGui();
	void Update();

	bool mRecomputeRequested = true;
};

struct CloudResources
{
	ComPtr<ID3D12Resource> mConstantUploadBuffer;
	void* mConstantUploadBufferPointer = nullptr;

	Shader mShapeNoiseShader = Shader().CSName(L"CloudShapeNoiseCS");
	Shader mErosionNoiseShader = Shader().CSName(L"CloudErosionNoiseCS");

	// Put shaders in array to use in loop
	std::vector<Shader*> mShaders =
	{
		&mShapeNoiseShader,
		&mErosionNoiseShader
	};

	Texture mShapeNoiseTexture = Texture().Width(128).Height(128).Depth(128).Format(DXGI_FORMAT_R8_UNORM).Name("Cloud.ShapeNoise").UIScale(2.0f).Path(L"Asset/TileableVolumeNoise/noiseShapePacked.tga");
	Texture mErosionNoiseTexture = Texture().Width(32).Height(32).Depth(32).Format(DXGI_FORMAT_R8_UNORM).Name("Cloud.ErosionNoise").UIScale(8.0f).Path(L"Asset/TileableVolumeNoise/noiseErosionPacked.tga");

	// Put textures in array to use in loop
	std::vector<Texture*> mTextures =
	{
		&mShapeNoiseTexture,
		&mErosionNoiseTexture
	};

	CloudResources()
	{
	}
};

extern CloudProfile gCloudProfile;
extern Cloud gCloud;
extern CloudResources gCloudResources;