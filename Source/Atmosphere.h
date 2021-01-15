#pragma once

#include "Common.h"

struct AtmosphereProfile
{
	// Data from [BN08]

	// Geometry
	double kBottomRadius						= 6360000.0;							// m
	double kAtmosphereThickness					= 60000.0;								// m
	double BottomRadius() const					{ return kBottomRadius; }
	double TopRadius() const					{ return kBottomRadius + kAtmosphereThickness; }

	// Scattering
	glm::dvec3 mRayleighScatteringCoefficient	= glm::vec3(5.8f, 13.5f, 33.1f);		// 10^-6 m^-1	
	glm::dvec3 mMieScatteringCoefficient		= glm::vec3(20.0, 20.0, 20.0);			// 10^-6 m^-1	
	glm::dvec3 mMieAbsorptionCoefficient		= mMieScatteringCoefficient / 0.9;		// 10^-6 m^-1

	double kRayleigh							= 1.24062e-6;
	double kRayleighScaleHeight					= 8000.0;								// m_
	double kMieAngstromAlpha					= 0.0;
	double kMieAngstromBeta						= 5.328e-3;
	double kMieSingleScatteringAlbedo			= 0.9;
	double kMieScaleHeight						= 1200.0;								// m

	double kLambdaR								= 680.0;								// nm
	double kLambdaG								= 550.0;								// nm
	double kLambdaB								= 440.0;								// nm

	double kOZoneBottomHeight					= 10000.0;								// m
	double kOZoneMidHeight						= 25000.0;								// m
	double kOZoneTopHeight						= 40000.0;								// m
};

class PrecomputedAtmosphereScattering
{
public:
	void Initialize();

	void ComputeTransmittance()
	{

	}

	void ComputeDirectIrradiance()
	{

	}

	void ComputeSingleScattering()
	{

	}

	void ComputeScatteringDensity()
	{

	}

	void ComputeIndirectIrradiance()
	{

	}

	void AccumulateMultipleScattering()
	{

	}

	void ComputeMultipleScattering()
	{
		ComputeScatteringDensity();
		ComputeIndirectIrradiance();
		AccumulateMultipleScattering();
	}

	void Precompute()
	{
		ComputeTransmittance();
		ComputeDirectIrradiance();
		ComputeSingleScattering();
		ComputeMultipleScattering();
	}
};

struct PrecomputedAtmosphereScatteringResources
{
	void Update();
	void Reset() { *this = PrecomputedAtmosphereScatteringResources(); }

	SystemShader mComputeTransmittanceShader;

	ComPtr<ID3D12Resource> mConstantUploadBuffer;
	void* mConstantUploadBufferPointer = nullptr;

	glm::uint32 mTransmittanceTextureWidth = 256;
	glm::uint32 mTransmittanceTextureHeight = 64;
	ComPtr<ID3D12Resource> mTransmittanceTexture;
	D3D12_CPU_DESCRIPTOR_HANDLE mTransmittanceTextureCPUHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE mTransmittanceTextureGPUHandle;
};

extern AtmosphereProfile gAtmosphereProfile;
extern PrecomputedAtmosphereScattering gPrecomputedAtmosphereScattering;
extern PrecomputedAtmosphereScatteringResources gPrecomputedAtmosphereScatteringResources;