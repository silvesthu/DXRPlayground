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
	double kBottomRadius							= 6360000.0;										// m
	double kAtmosphereThickness						= 60000.0;											// m
	double BottomRadius() const						{ return kBottomRadius; }
	double TopRadius() const						{ return kBottomRadius + kAtmosphereThickness; }
	double kRayleighScaleHeight						= 8000.0;											// m
	double kMieScaleHeight							= 1200.0;											// m
	double kOzoneBottomAltitude						= 10000.0;											// m
	double kOzoneMidAltitude						= 25000.0;											// m
	double kOzoneTopAltitude						= 40000.0;											// m

	// Rayleigh [BN08][BN08 Code]
	enum class RayleighMode
	{
		BN08Impl,
		BN08,
		PSS99
	};
	RayleighMode mRayleighMode						= RayleighMode::BN08Impl;
	glm::dvec3 mRayleighScatteringCoefficient		= glm::dvec3(0);									// m^-1

	double kLambdaR									= 680.0;											// nm
	double kLambdaG									= 550.0;											// nm
	double kLambdaB									= 440.0;											// nm
	glm::dvec3 kLambda								= glm::dvec3(kLambdaR, kLambdaG, kLambdaB);			// nm
	double kRayleigh								= 1.24062e-6 * 1e-24;								// m^4 <- um^4 (?)
	glm::dvec3 mRayleighScatteringCoefficient_		= glm::dvec3(5.8, 13.5, 33.1) * 1e-6;				// m^-1

	// Mie
	enum class MieMode
	{
		BN08Impl,
		BN08,
	};
	MieMode mMieMode								= MieMode::BN08Impl;
	glm::dvec3 mMieExtinctionCoefficient			= glm::dvec3(0.0);									// Assign during update
	glm::dvec3 mMieScatteringCoefficient			= glm::dvec3(0.0);									// Assign during update	

	double kMieAngstromAlpha						= 0.0;
	double kMieAngstromBeta							= 5.328e-3;
	double kMieSingleScatteringAlbedo				= 0.9;

	glm::dvec3 mMieScatteringCoefficientPaper		= glm::dvec3(20.0, 20.0, 20.0) * 1e-6;				// m^-1
	glm::dvec3 mMieExtinctionCoefficientPaper		= mMieScatteringCoefficientPaper / 0.9;				// m^-1

	bool kEnableOzone								= true;

	// Turbidity [PSS99][ZWP07]
	double kTurbidity								= 1;												// 1 ~ Pure air, >= 10 ~ Haze
};

class PrecomputedAtmosphereScattering
{
public:
	void Initialize();
	void Finalize();
	void UpdateImGui();
	void Update();
	void Render();

	void ComputeTransmittance();
	void ComputeDirectIrradiance();
	void ComputeSingleScattering();
	void ComputeScatteringDensity();
	void ComputeIndirectIrradiance();
	void AccumulateMultipleScattering();

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

	float mUIScale = 1.0f;
	bool mUIFlipY = false;
	bool mUnifyXYEncode = false;
};

struct PrecomputedAtmosphereScatteringResources
{
	ComPtr<ID3D12Resource> mConstantUploadBuffer;
	void* mConstantUploadBufferPointer = nullptr;

	Shader mComputeTransmittanceShader = Shader().CSName(L"ComputeTransmittanceCS");
	Shader mComputeDirectIrradianceShader = Shader().CSName(L"ComputeDirectIrradianceCS");

	std::vector<Shader*> mShaders = 
	{ 
		&mComputeTransmittanceShader, 
		&mComputeDirectIrradianceShader 
	};
	 
	Texture mTransmittanceTexture = Texture().Width(256).Height(64).Name("Transmittance");
	Texture mDeltaIrradianceTexture = Texture().Width(64).Height(16).Name("DeltaIrradiance").UIScale(4.0f);
	Texture mIrradianceTexture = Texture().Width(64).Height(16).Name("Irradiance").UIScale(4.0f);

	std::vector<Texture*> mTextures =
	{
		&mTransmittanceTexture,
		&mDeltaIrradianceTexture,
		&mIrradianceTexture
	};
};

extern AtmosphereProfile gAtmosphereProfile;
extern PrecomputedAtmosphereScattering gPrecomputedAtmosphereScattering;
extern PrecomputedAtmosphereScatteringResources gPrecomputedAtmosphereScatteringResources;