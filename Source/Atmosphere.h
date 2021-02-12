#pragma once

#include "Common.h"

namespace UnitHelper
{
	template <typename ToType, typename FromType>
	static ToType stMeterToKilometer(const FromType& meter)
	{
		return ToType(meter * 1e-3);
	}

	template <typename ToType, typename FromType>
	static ToType sNanometerToMeter(const FromType& nanometer)
	{
		return ToType(nanometer * 1e-9);
	}

	template <typename ToType, typename FromType>
	static ToType sInverseMeterToInverseKilometer(const FromType& inverse_meter)
	{
		return ToType(inverse_meter * 1e3);
	}

	template <typename ToType, typename FromType>
	static ToType sInverseNanometerToInverseKilometer(const FromType& inverse_nanometer)
	{
		return ToType(inverse_nanometer * 1e12);
	}
}

struct AtmosphereProfile
{
	// [Nishita93][NSTN93] Display of The Earth Taking into Account Atmospheric Scattering https://www.researchgate.net/publication/2933032_Display_of_The_Earth_Taking_into_Account_Atmospheric_Scattering
	// [Preetham99][PSS99] A Practical Analytic Model for Daylight https://www2.cs.duke.edu/courses/cps124/spring08/assign/07_papers/p91-preetham.pdf
	// [Riley04][REK*04] Efficient Rendering of Atmospheric Phenomena https://people.cs.clemson.edu/~jtessen/reports/papers_files/Atmos_EGSR_Elec.pdf
	// [Zotti07][ZWP07] A Critical Review of the Preetham Skylight Model https://www.cg.tuwien.ac.at/research/publications/2007/zotti-2007-wscg/zotti-2007-wscg-paper.pdf
	// [Bruneton08] Precomputed Atmospheric Scattering https://hal.inria.fr/inria-00288758/document
	// [Bruneton08Doc] https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html
	// [Bruneton08Impl] https://github.com/ebruneton/precomputed_atmospheric_scattering
	// [Elek09] Rendering Parametrizable Planetary Atmospheres with Multiple Scattering in Real-Time http://www.klayge.org/material/4_0/Atmospheric/Rendering%20Parametrizable%20Planetary%20Atmospheres%20with%20Multiple%20Scattering%20in%20Real-Time.pdf
	// [Yusov13] Outdoor Light Scattering Sample Update https://software.intel.com/content/www/us/en/develop/blogs/otdoor-light-scattering-sample-update.html
	// [Hillaire16] Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016-pbs-frostbite-sky-clouds-new.pdf
	// [Carpentier17] Decima Engine : Advances in Lighting and AA https://www.guerrilla-games.com/read/decima-engine-advances-in-lighting-and-aa
	// [Hillaire20] A Scalable and Production Ready Sky and Atmosphere Rendering Technique https://sebh.github.io/publications/egsr2020.pdf
	// [UE4 SkyAtmosphere] SkyAtmosphere.usf SkyAtmosphereComponent.cpp https://docs.unrealengine.com/en-US/BuildingWorlds/FogEffects/SkyAtmosphere/index.html

	// Wavelength
	static constexpr double kLambdaR					= 680.0;											// nm
	static constexpr double kLambdaG					= 550.0;											// nm
	static constexpr double kLambdaB					= 440.0;											// nm
	static constexpr glm::dvec3 kLambda					= glm::dvec3(kLambdaR, kLambdaG, kLambdaB);			// nm

	// Geometry
	struct GeometryReference
	{
		static void Bruneton08Impl(AtmosphereProfile& profile)
		{
			profile.mBottomRadius						= 6360000.0;										// m
			profile.mAtmosphereThickness				= 60000.0;											// m
		}
	};

	double mBottomRadius								= {};												// m
	double mAtmosphereThickness							= {};												// m
	double BottomRadius() const							{ return mBottomRadius; }							// m
	double TopRadius() const							{ return mBottomRadius + mAtmosphereThickness; }	// m

	// Rayleigh
	struct RayleighReference
	{
		static void Bruneton08Impl(AtmosphereProfile& profile)
		{
			static constexpr double kRayleighScaleHeight = 8000.0;

			constexpr double kRayleigh = 1.24062e-6 * 1e-24; // m^4 <- um^4 (?)
			profile.mRayleighScatteringCoefficient = kRayleigh / glm::pow(UnitHelper::sNanometerToMeter<glm::dvec3>(AtmosphereProfile::kLambda), glm::dvec3(4.0));

			// [Bruneton08] 2.1 (1), e^(-1/H_R) Density decrease exponentially
			static constexpr float kDummy = 0.0f;
			profile.mRayleighDensityProfile.mLayer0 = { kDummy, kDummy, kDummy, kDummy, kDummy };
			profile.mRayleighDensityProfile.mLayer1 = { kDummy, 1.0f, -1.0f / UnitHelper::stMeterToKilometer<float>(kRayleighScaleHeight), 0.0f, 0.0f };

			// How to get scale height?
			// https://en.wikipedia.org/wiki/Scale_height
		}

		static void Bruneton08(AtmosphereProfile& profile) // 2.1 [REK*04] Table 3
		{
			Bruneton08Impl(profile);

			profile.mRayleighScatteringCoefficient = glm::dvec3(5.8, 13.5, 33.1) * 1e-6; // m^-1
		}

		static void PSS99(AtmosphereProfile& profile)
		{
			Bruneton08Impl(profile);

			constexpr double pi = glm::pi<double>();
			double n = 1.0003; // index of refraction of air
			double N = 2.545e25; // number of molecules per unit volume
			double p_n = 0.035; // depolarization factor
			double kRayleigh =
				((8.0 * glm::pow(pi, 3.0) * glm::pow((n * n - 1.0), 2.0)) * (6.0 + 3.0 * p_n)) // Why [Bruneton08] 2.1 (1) omit the right half?
				/ // -----------------------------------------------------------------------------------------
				((3.0 * N) * (6.0 - 7.0 * p_n));
			profile.mRayleighScatteringCoefficient = kRayleigh / glm::pow(UnitHelper::sNanometerToMeter<glm::dvec3>(AtmosphereProfile::kLambda), glm::dvec3(4.0));

			// [NOTE]
			// Total Scattering Coefficient = Integral of Angular Scattering Coefficient in all directions
			// Angular Scattering Coefficient = Total Scattering Coefficient * (1 + cos(theta)^2) * 3.0 / 2.0
		}
	};
	ShaderType::DensityProfile mRayleighDensityProfile	= {}; // km
	glm::dvec3 mRayleighScatteringCoefficient			= {}; // m^-1

	// Mie
	struct MieReference
	{
		static void Bruneton08Impl(AtmosphereProfile& profile)
		{
			static constexpr double kMieScaleHeight = 1200.0;

			static constexpr double kMieAngstromAlpha = 0.0;
			static constexpr double kMieAngstromBeta = 5.328e-3;
			static constexpr double kMieSingleScatteringAlbedo = 0.9;

			// [TODO] Different from paper?
			profile.mMieExtinctionCoefficient = kMieAngstromBeta / kMieScaleHeight * glm::pow(profile.kLambda, glm::dvec3(-kMieAngstromAlpha));
			profile.mMieScatteringCoefficient = profile.mMieExtinctionCoefficient * kMieSingleScatteringAlbedo;

			profile.mMiePhaseFunctionG = 0.8;

			// [Bruneton08] 2.1 (3), e^(-1/H_M) Density decrease exponentially
			static constexpr float kDummy = 0.0f;
			profile.mMieDensityProfile.mLayer0 = { kDummy, kDummy, kDummy, kDummy, kDummy };
			profile.mMieDensityProfile.mLayer1 = { kDummy, 1.0f, -1.0f / UnitHelper::stMeterToKilometer<float>(kMieScaleHeight), 0.0f, 0.0f };
		}

		static void Bruneton08(AtmosphereProfile& profile) // 2.1 (3), beta_M(0, lambda)
		{
			Bruneton08Impl(profile);

			profile.mMieScatteringCoefficient = glm::dvec3(20.0, 20.0, 20.0) * 1e-6;
			profile.mMieExtinctionCoefficient = profile.mMieScatteringCoefficient / 0.9;
		}
	};
	ShaderType::DensityProfile mMieDensityProfile		= {}; // km
	glm::dvec3 mMieScatteringCoefficient				= {}; // m^-1
	glm::dvec3 mMieExtinctionCoefficient				= {}; // m^-1
	double mMiePhaseFunctionG							= {};

	// Ozone 
	struct OzoneReference
	{
		static void UpdateDensityProfile(AtmosphereProfile& profile)
		{
			// Density increase linearly, then decrease linearly
			float ozone_bottom_altitude = UnitHelper::stMeterToKilometer<float>(profile.mOzoneBottomAltitude);
			float ozone_mid_altitude = UnitHelper::stMeterToKilometer<float>(profile.mOzoneMidAltitude);
			float ozone_top_altitude = UnitHelper::stMeterToKilometer<float>(profile.mOzoneTopAltitude);
			float layer_0_linear_term, layer_0_constant_term, layer_1_linear_term, layer_1_constant_term;
			{
				// Altitude -> Density
				auto calculate_linear_term = [](float inX0, float inX1, float& outLinearTerm, float& outConstantTerm)
				{
					outLinearTerm = 1.0f / (inX1 - inX0);
					outConstantTerm = 1.0f * (0.0f - inX0) / (inX1 - inX0);
				};
				calculate_linear_term(ozone_bottom_altitude, ozone_mid_altitude, layer_0_linear_term, layer_0_constant_term);
				calculate_linear_term(ozone_top_altitude, ozone_mid_altitude, layer_1_linear_term, layer_1_constant_term);
			}
			static constexpr float kDummy = 0.0f;
			profile.mOzoneDensityProfile.mLayer0 = { ozone_mid_altitude, 0.0f, 0.0f, layer_0_linear_term, layer_0_constant_term };
			profile.mOzoneDensityProfile.mLayer1 = { kDummy, 0.0f, 0.0f, layer_1_linear_term, layer_1_constant_term };
		}

		static void Bruneton08Impl(AtmosphereProfile& profile)				
		{ 
			profile.mEnableOzone = true;

			profile.mOzoneBottomAltitude				= 10000.0;
			profile.mOzoneMidAltitude					= 25000.0;
			profile.mOzoneTopAltitude					= 40000.0;

			constexpr double kDobsonUnit = 2.687e20; // m^-2
			constexpr double kMaxOzoneNumberDensity	= 300.0 * kDobsonUnit / 15000.0; // m^-2
			constexpr glm::dvec3 kOzoneCrossSection	= glm::dvec3(1.209e-25, 3.5e-25, 1.582e-26); // m^2
			profile.mOZoneAbsorptionCoefficient = kMaxOzoneNumberDensity * kOzoneCrossSection;

			UpdateDensityProfile(profile);
		}
	};
	bool mEnableOzone									= {};
	double mOzoneBottomAltitude							= {}; // m
	double mOzoneMidAltitude							= {}; // m
	double mOzoneTopAltitude							= {}; // m
	ShaderType::DensityProfile mOzoneDensityProfile		= {}; // km
	glm::dvec3 mOZoneAbsorptionCoefficient				= {}; // m^-1

	// Solar
	struct SolarIrradianceReference
	{
		// http://rredc.nrel.gov/solar/spectra/am1.5/ASTMG173/ASTMG173.html
		static void Bruneton08ImplConstant(AtmosphereProfile& profile)		
		{ 
			profile.mSolarIrradiance = glm::vec3(1.5f); 
		}

		static void Bruneton08Impl(AtmosphereProfile& profile)
		{ 
			profile.mSolarIrradiance = glm::vec3(1.474000f, 1.850400f, 1.911980f); 
		}
	};
	glm::vec3 mSolarIrradiance							= {}; // W.m^-2
	// [Note] Calculated based on Sun seen from Earth
	// https://sciencing.com/calculate-angular-diameter-sun-8592633.html
	// Angular Radius = Angular Diameter / 2.0 = arctan(Sun radius / Sun-Earth distance)
	double kSunAngularRadius							= 0.00935f / 2.0f; // Radian, from [Bruneton08Impl] demo.cc

	// Multiple scattering
	glm::uint mScatteringOrder							= 4;

	// Ground
	bool mAerialPerspective								= true;
	glm::vec3 mGroundAlbedo								= glm::vec3(0.1f);
	glm::vec3 mRuntimeGroundAlbedo						= glm::vec3(0.0f, 0.0f, 0.04f);

	// Unit
	float mSceneScale									= 1.0;

	// Default
	AtmosphereProfile()
	{
		GeometryReference::Bruneton08Impl(*this);
		RayleighReference::Bruneton08Impl(*this);
		MieReference::Bruneton08Impl(*this);
		OzoneReference::Bruneton08Impl(*this);
		SolarIrradianceReference::Bruneton08Impl(*this);
	}
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

	void ComputeScatteringDensity(glm::uint scattering_order);
	void ComputeIndirectIrradiance(glm::uint scattering_order);
	void AccumulateMultipleScattering();

	void ComputeMultipleScattering(glm::uint scattering_order);

	void Precompute();

	bool mRecomputeRequested = true;
	bool mRecomputeEveryFrame = false;

	float mUIScale = 1.0f;
	bool mUIFlipY = false;
};

struct PrecomputedAtmosphereScatteringResources
{
	ComPtr<ID3D12Resource> mConstantUploadBuffer;
	void* mConstantUploadBufferPointer = nullptr;

	Shader mComputeTransmittanceShader			= Shader().CSName(L"ComputeTransmittanceCS");
	Shader mComputeDirectIrradianceShader		= Shader().CSName(L"ComputeDirectIrradianceCS");
	Shader mComputeSingleScatteringShader		= Shader().CSName(L"ComputeSingleScatteringCS");

	Shader mComputeScatteringDensityShader		= Shader().CSName(L"ComputeScatteringDensityCS");
	Shader mComputeIndirectIrradianceShader		= Shader().CSName(L"ComputeIndirectIrradianceCS");
	Shader mComputeMultipleScatteringShader		= Shader().CSName(L"ComputeMultipleScatteringCS");

	// Put shaders in array to use in loop (use std::span when C++20 is available)
	std::vector<Shader*> mShaders = 
	{ 
		&mComputeTransmittanceShader, 
		&mComputeDirectIrradianceShader,
		&mComputeSingleScatteringShader,

		&mComputeScatteringDensityShader,
		&mComputeIndirectIrradianceShader,
		&mComputeMultipleScatteringShader
	};

	Texture mTransmittanceTexture				= Texture().Width(256).Height(64).Format(DXGI_FORMAT_R32G32B32A32_FLOAT).Name("Transmittance");

	Texture mDeltaIrradianceTexture				= Texture().Width(64).Height(16).Format(DXGI_FORMAT_R32G32B32A32_FLOAT).Name("Delta Irradiance").UIScale(4.0f);
	Texture mIrradianceTexture					= Texture().Width(64).Height(16).Format(DXGI_FORMAT_R32G32B32A32_FLOAT).Name("Irradiance").UIScale(4.0f);

	glm::uint mXSliceCount						= 8; // Slice X axis to use 3D texture as 4D storage
	Texture mDeltaRayleighScatteringTexture		= Texture().Width(256).Height(128).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Rayleigh Scattering");
	Texture mDeltaMieScatteringTexture			= Texture().Width(256).Height(128).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Mie Scattering");
	Texture mScatteringTexture					= Texture().Width(256).Height(128).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Scattering");

	Texture mDeltaScatteringDensityTexture		= Texture().Width(256).Height(128).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Scattering Density");
	
	// Put textures in array to use in loop
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