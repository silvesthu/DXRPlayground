#pragma once

#include "Common.h"

struct DDGIProbe
{
	glm::vec4 mPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

	Texture mIrradianceTexture = Texture().Width(8).Height(8).UIScale(32.0f).Format(DXGI_FORMAT_R11G11B10_FLOAT).Name("DDGIProbe.Irradiance");
	Texture mDistanceTexture = Texture().Width(16).Height(16).UIScale(16.0f).Format(DXGI_FORMAT_R16G16_FLOAT).Name("DDGIProbe.Distance");
};

class DDGI
{
public:
	void Initialize();
	void Precompute();
	void Finalize();
	void UpdateImGui();
	void Update();

	bool mRecomputeRequested = true;
};

struct DDGIResources
{
	ComPtr<ID3D12Resource> mConstantUploadBuffer;
	void* mConstantUploadBufferPointer = nullptr;

	Shader mPrecomputeShader = Shader().CSName(L"DDGIPrecompute");

	// Put shaders in array to use in loop
	std::vector<Shader*> mShaders =
	{
		&mPrecomputeShader
	};

	std::vector<DDGIProbe> mProbes;
	DDGIProbe mDummyProbe;
};

extern DDGI gDDGI;
extern DDGIResources gDDGIResources;