#pragma once

#include "Common.h"

// [Schneider16] GPU Pro 7 Advanced Rendering Techniques, Real-Time Volumetric Cloudscapes

enum class CloudMode
{
	None = 0,

	RuntimeNoise,

	Count
};

struct CloudProfile
{
	CloudMode mMode = CloudMode::RuntimeNoise;
};

class Cloud
{
public:
	void Initialize();
	void Finalize();
	void UpdateImGui();
	void Update();
};

struct CloudResources
{
	ComPtr<ID3D12Resource> mConstantUploadBuffer;
	void* mConstantUploadBufferPointer = nullptr;
};

extern CloudProfile gCloudProfile;
extern Cloud gCloud;
extern CloudResources gCloudResources;