#pragma once

#include "Common.h"

struct AtmosphereProfile
{
public:
	void Reset() { *this = AtmosphereProfile(); }

	// Earth
	glm::f64 mEarthRadius						= 6360000.0;									// m
	glm::f64 mAtmosphereThickness				= 60000.0;										// m
	glm::f64 AtmosphereBottomRadius() const		{ return mEarthRadius; }						// m
	glm::f64 AtmosphereTopRadius() const		{ return mEarthRadius + mAtmosphereThickness; } // m

	// Scattering, default from [BN08]
	glm::dvec3 mRayleighScatteringCoefficient	= glm::vec3(5.8f, 13.5f, 33.1f);				// 10^-6 m^-1
	glm::f64 mRayleighScaleHeight				= 8000.0;										// m
	glm::dvec3 mMieScatteringCoefficient		= glm::vec3(20.0, 20.0, 20.0);					// 10^-6 m^-1
	glm::f64 mMieScaleHeight					= 1200.0;										// m
	glm::dvec3 mMieAbsorptionCoefficient		= mMieScatteringCoefficient / 0.9;				// 10^-6 m^-1
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