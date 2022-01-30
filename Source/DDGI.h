#pragma once
#include "Common.h"

// [McGuire19]			Dynamic Diffuse Global Illumination											https://morgan3d.github.io/articles/2019-04-01-ddgi/
// [Majercik2019]		Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields		https://jcgt.org/published/0008/02/01/

class DDGI
{
public:
	struct Probe
	{
		glm::vec4 mPosition				= glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		Texture mIrradianceTexture		= Texture().Width(8).Height(8).UIScale(32.0f).Format(DXGI_FORMAT_R11G11B10_FLOAT).Name("DDGI.Probe.Irradiance");
		Texture mDistanceTexture		= Texture().Width(16).Height(16).UIScale(16.0f).Format(DXGI_FORMAT_R16G16_FLOAT).Name("DDGI.Probe.Distance");
	};

	struct Resource : ResourceBase<Resource>
	{
		ComPtr<ID3D12Resource> mConstantUploadBuffer;
		void* mConstantUploadBufferPointer = nullptr;

		// Shaders
		Shader mPrecomputeShader		= Shader().CSName(L"DDGIPrecompute");
		// Gather shaders
		Shader mSentinelShader			= Shader();
		std::span<Shader> mShaders		= std::span<Shader>(&mPrecomputeShader, &mSentinelShader);

		std::vector<Probe> mProbes		= {};
	};
	Resource mResource;

	void Initialize();
	void Precompute();
	void Finalize();
	void UpdateImGui();
	void Update();

	bool mRecomputeRequested = true;
};

extern DDGI gDDGI;