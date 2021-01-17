#pragma once

#include "Common.h"

struct AtmosphereProfile
{
	// [BN08] https://hal.inria.fr/inria-00288758/document
	// [BN08 Code] https://github.com/ebruneton/precomputed_atmospheric_scattering
	// [REK*04] https://people.cs.clemson.edu/~jtessen/reports/papers_files/Atmos_EGSR_Elec.pdf
	// [PSS99] https://www2.cs.duke.edu/courses/cps124/spring08/assign/07_papers/p91-preetham.pdf
	// [ZWP07] A Critical Review of the Preetham Skylight Model https://www.cg.tuwien.ac.at/research/publications/2007/zotti-2007-wscg/zotti-2007-wscg-paper.pdf

	// Geometry [BN08]
	double kBottomRadius						= 6360000.0;										// m
	double kAtmosphereThickness					= 60000.0;											// m
	double BottomRadius() const					{ return kBottomRadius; }
	double TopRadius() const					{ return kBottomRadius + kAtmosphereThickness; }
	double kRayleighScaleHeight					= 8000.0;											// m
	double kMieScaleHeight						= 1200.0;											// m
	double kOzoneBottomAltitude					= 10000.0;											// m
	double kOzoneMidAltitude					= 25000.0;											// m
	double kOzoneTopAltitude					= 40000.0;											// m

	// Wavelength -> Coefficient [BN08][BN08 Code][TS99]
	bool kWavelengthToCoefficient				= true;

	bool kUsePSS99								= true;
	double kLambdaR								= 680.0;											// nm
	double kLambdaG								= 550.0;											// nm
	double kLambdaB								= 440.0;											// nm
	glm::dvec3 kLambda							= glm::dvec3(kLambdaR, kLambdaG, kLambdaB);			// nm
	double kRayleigh							= 1.24062e-6 * 1e-24;								// m^4 <- um^4 (?)

	double kMieAngstromAlpha					= 0.0;
	double kMieAngstromBeta						= 5.328e-3;
	double kMieSingleScatteringAlbedo			= 0.9;

	bool kUseOzone								= true;

	// Reference Coefficient [BN08][BN08 Code][REK*04]
	glm::dvec3 mRayleighScatteringCoefficient	= glm::dvec3(5.8, 13.5, 33.1) * 1e-6;				// m^-1
	glm::dvec3 mMieScatteringCoefficient		= glm::dvec3(20.0, 20.0, 20.0) * 1e-6;				// m^-1
	glm::dvec3 mMieExtinctionCoefficient		= glm::dvec3(4.44, 4.44, 4.44) * 1e-6;				// m^-1
	glm::dvec3 mMieAbsorptionCoefficient		= mMieScatteringCoefficient / 0.9;					// m^-1
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