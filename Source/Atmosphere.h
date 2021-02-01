#pragma once

#include "Common.h"

struct AtmosphereProfile
{
	// [Nishita93][NSTN93] Display of The Earth Taking into Account Atmospheric Scattering https://www.researchgate.net/publication/2933032_Display_of_The_Earth_Taking_into_Account_Atmospheric_Scattering
	// [Preetham99][PSS99] A Practical Analytic Model for Daylight https://www2.cs.duke.edu/courses/cps124/spring08/assign/07_papers/p91-preetham.pdf
	// [Riley04][REK*04] Efficient Rendering of Atmospheric Phenomena https://people.cs.clemson.edu/~jtessen/reports/papers_files/Atmos_EGSR_Elec.pdf
	// [Zotti07][ZWP07] A Critical Review of the Preetham Skylight Model https://www.cg.tuwien.ac.at/research/publications/2007/zotti-2007-wscg/zotti-2007-wscg-paper.pdf
	// [Bruneton08] Precomputed Atmospheric Scattering https://hal.inria.fr/inria-00288758/document
	// [Bruneton08 Impl] https://github.com/ebruneton/precomputed_atmospheric_scattering
	// [Elek09] Rendering Parametrizable Planetary Atmospheres with Multiple Scattering in Real-Time http://www.klayge.org/material/4_0/Atmospheric/Rendering%20Parametrizable%20Planetary%20Atmospheres%20with%20Multiple%20Scattering%20in%20Real-Time.pdf
	// [Yusov13] Outdoor Light Scattering Sample Update https://software.intel.com/content/www/us/en/develop/blogs/otdoor-light-scattering-sample-update.html

	// Geometry [Bruneton08]
	double kBottomRadius							= 6360000.0;										// m
	double kAtmosphereThickness						= 60000.0;											// m
	double BottomRadius() const						{ return kBottomRadius; }							// m
	double TopRadius() const						{ return kBottomRadius + kAtmosphereThickness; }	// m
	double kRayleighScaleHeight						= 8000.0;											// m
	double kMieScaleHeight							= 1200.0;											// m
	double kOzoneBottomAltitude						= 10000.0;											// m
	double kOzoneMidAltitude						= 25000.0;											// m
	double kOzoneTopAltitude						= 40000.0;											// m

	// Geometry Alternative
	double kRayleighScaleHeight_NSTN93				= 7994.0;											// m

	// Rayleigh [Bruneton08][Bruneton08 Impl]
	enum class RayleighMode
	{
		Precomputed,
		Bruneton08Impl,
		PSS99
	};
	RayleighMode mRayleighMode						= RayleighMode::Precomputed;
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
		Bruneton08Impl,
		Bruneton08,
	};
	MieMode mMieMode								= MieMode::Bruneton08Impl;
	glm::dvec3 mMieExtinctionCoefficient			= glm::dvec3(0.0);									// m^-1
	glm::dvec3 mMieScatteringCoefficient			= glm::dvec3(0.0);									// m^-1

	double kMieAngstromAlpha						= 0.0;
	double kMieAngstromBeta							= 5.328e-3;
	double kMieSingleScatteringAlbedo				= 0.9;

	glm::dvec3 mMieScatteringCoefficientPaper		= glm::dvec3(20.0, 20.0, 20.0) * 1e-6;				// m^-1
	glm::dvec3 mMieExtinctionCoefficientPaper		= mMieScatteringCoefficientPaper / 0.9;				// m^-1

	// Ozone 
	// [Bruneton08 Impl]
	bool mEnableOzone								= true;
	double kDobsonUnit								= 2.687e20;											// m^-2
	double kMaxOzoneNumberDensity					= 300.0 * kDobsonUnit / 15000.0;					// m^-2
	glm::dvec3 kOzoneCrossSection					= glm::dvec3(1.209e-25, 3.5e-25, 1.582e-26);		// m^2

	glm::dvec3 mOZoneAbsorptionCoefficient			= glm::dvec3(0);									// m^-1

	// Turbidity [PSS99][ZWP07]
	double kTurbidity								= 1;												// 1 ~ Pure air, >= 10 ~ Haze

	// Solar 
	// [Bruneton08 Impl] demo.cc http://rredc.nrel.gov/solar/spectra/am1.5/ASTMG173/ASTMG173.html
	bool mUseConstantSolarIrradiance				= false;
	glm::dvec3 kConstantSolarIrradiance				= glm::dvec3(1.5);									// W.m^-2
	glm::dvec3 kSolarIrradiance						= glm::dvec3(1.474000, 1.850400, 1.911980);			// W.m^-2

	// Sun
	// [Bruneton08 Impl] demo.cc
	double kSunAngularRadius						= 0.00935f / 2.0f;									// Radian
	// [Note] Calculated based on Sun seen from Earth
	// https://sciencing.com/calculate-angular-diameter-sun-8592633.html
	// Angular Radius = Angular Diameter / 2.0 = arctan(Sun radius / Sun-Earth distance)
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

	void Precompute();

	float mUIScale = 1.0f;
	bool mUIFlipY = false;
	bool mTrivialAxisEncoding = false;
};

struct PrecomputedAtmosphereScatteringResources
{
	ComPtr<ID3D12Resource> mConstantUploadBuffer;
	void* mConstantUploadBufferPointer = nullptr;

	Shader mComputeTransmittanceShader = Shader().CSName(L"ComputeTransmittanceCS");
	Shader mComputeDirectIrradianceShader = Shader().CSName(L"ComputeDirectIrradianceCS");
	Shader mComputeSingleScatteringShader = Shader().CSName(L"ComputeSingleScatteringCS");

	// std::span is better but requires C++20
	std::vector<Shader*> mShaders = 
	{ 
		&mComputeTransmittanceShader, 
		&mComputeDirectIrradianceShader,
		&mComputeSingleScatteringShader
	};

	Texture mTransmittanceTexture = Texture().Width(256).Height(64).Name("Transmittance");

	Texture mDeltaIrradianceTexture = Texture().Width(64).Height(16).Name("DeltaIrradiance").UIScale(4.0f);
	Texture mIrradianceTexture = Texture().Width(64).Height(16).Name("Irradiance").UIScale(4.0f);

	glm::uint mXBinCount = 8;
	Texture mDeltaRayleighScatteringTexture = Texture().Width(256).Height(128).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Rayleigh Scattering");
	Texture mDeltaMieScatteringTexture = Texture().Width(256).Height(128).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Mie Scattering");
	Texture mScatteringTexture = Texture().Width(256).Height(128).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Scattering");

	Texture mDeltaScatteringDensityTexture = Texture().Width(256).Height(128).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Scattering Density");

	std::vector<Texture*> mTextures =
	{
		&mTransmittanceTexture,
		&mDeltaIrradianceTexture,
		&mIrradianceTexture,
		&mDeltaRayleighScatteringTexture,
		&mDeltaMieScatteringTexture,
		&mScatteringTexture,
		&mDeltaScatteringDensityTexture
	};
};

extern AtmosphereProfile gAtmosphereProfile;
extern PrecomputedAtmosphereScattering gPrecomputedAtmosphereScattering;
extern PrecomputedAtmosphereScatteringResources gPrecomputedAtmosphereScatteringResources;