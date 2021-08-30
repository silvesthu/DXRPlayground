#pragma once

#include "Common.h"

// [Nishita93][NSTN93] Display of The Earth Taking into Account Atmospheric Scattering https://www.researchgate.net/publication/2933032_Display_of_The_Earth_Taking_into_Account_Atmospheric_Scattering
// [Preetham99][PSS99] A Practical Analytic Model for Daylight https://www2.cs.duke.edu/courses/cps124/spring08/assign/07_papers/p91-preetham.pdf
// [Riley04][REK*04] Efficient Rendering of Atmospheric Phenomena https://people.cs.clemson.edu/~jtessen/reports/papers_files/Atmos_EGSR_Elec.pdf
// [Zotti07][ZWP07] A Critical Review of the Preetham Skylight Model https://www.cg.tuwien.ac.at/research/publications/2007/zotti-2007-wscg/zotti-2007-wscg-paper.pdf
// [Bruneton08] Precomputed Atmospheric Scattering https://hal.inria.fr/inria-00288758/document https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html
// [Elek09] Rendering Parametrizable Planetary Atmospheres with Multiple Scattering in Real-Time http://www.klayge.org/material/4_0/Atmospheric/Rendering%20Parametrizable%20Planetary%20Atmospheres%20with%20Multiple%20Scattering%20in%20Real-Time.pdf
// [Yusov13] Outdoor Light Scattering Sample Update https://software.intel.com/content/www/us/en/develop/blogs/otdoor-light-scattering-sample-update.html
// [Hillaire16] Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016-pbs-frostbite-sky-clouds-new.pdf
// [Bruneton17] https://github.com/ebruneton/precomputed_atmospheric_scattering
// [Carpentier17] Decima Engine : Advances in Lighting and AA https://www.guerrilla-games.com/read/decima-engine-advances-in-lighting-and-aa
// [Hillaire20] A Scalable and Production Ready Sky and Atmosphere Rendering Technique https://sebh.github.io/publications/egsr2020.pdf
// [Epic20] SkyAtmosphere.usf SkyAtmosphereComponent.cpp https://docs.unrealengine.com/en-US/BuildingWorlds/FogEffects/SkyAtmosphere/index.html

struct AtmosphereProfile
{
	// Config
	AtmosphereMode mMode								= AtmosphereMode::Hillaire20;

	// Constant Color
	glm::vec4 mConstantColor							= glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

	// Wavelength
	static constexpr double kLambdaR					= 680.0e-9;											// m <- nm
	static constexpr double kLambdaG					= 550.0e-9;											// m <- nm
	static constexpr double kLambdaB					= 440.0e-9;											// m <- nm
	static constexpr glm::dvec3 kLambda					= glm::dvec3(kLambdaR, kLambdaG, kLambdaB);			// m

	// Geometry
	struct GeometryReference
	{
		static void Bruneton17(AtmosphereProfile& profile)
		{
			profile.mBottomRadius						= 6360.0;											// km
			profile.mAtmosphereThickness				= 60.0;												// km
		}

		static void Yusov13(AtmosphereProfile& profile)
		{
			profile.mBottomRadius						= 6360.0;											// km
			profile.mAtmosphereThickness				= 80.0;												// km
		}

		static void Hillaire20(AtmosphereProfile& profile)
		{
			profile.mBottomRadius						= 6360.0;											// km
			profile.mAtmosphereThickness				= 100.0;											// km
		}
	};

	double mBottomRadius								= {};												// km
	double mAtmosphereThickness							= {};												// km
	double BottomRadius() const							{ return mBottomRadius; }							// km
	double TopRadius() const							{ return mBottomRadius + mAtmosphereThickness; }	// km

	// Rayleigh
	struct RayleighReference
	{
		static void Bruneton17(AtmosphereProfile& profile)
		{
			static constexpr double kRayleighScaleHeight = 8.0;

			constexpr double kRayleigh = 1.24062e-6 * 1e-24; // m^4 <- um^4 (?)
			profile.mRayleighScatteringCoefficient = kRayleigh / glm::pow(AtmosphereProfile::kLambda, glm::dvec3(4.0));
			profile.mRayleighScatteringCoefficient *= 1e3; // km^-1 <- m^-1

			// [Bruneton08] 2.1 (1), e^(-1/H_R) Density decrease exponentially
			static constexpr float kDummy = 0.0f;
			profile.mRayleighDensityProfile.mLayer0 = { kDummy, kDummy, kDummy, kDummy, kDummy };
			profile.mRayleighDensityProfile.mLayer1 = { kDummy, 1.0f, -1.0f / static_cast<float>(kRayleighScaleHeight), 0.0f, 0.0f };

			// How to get scale height?
			// https://en.wikipedia.org/wiki/Scale_height

			profile.mEnableRayleigh = true;
		}

		static void Bruneton08(AtmosphereProfile& profile) // 2.1 [REK*04] Table 3
		{
			Bruneton17(profile);

			profile.mRayleighScatteringCoefficient = glm::dvec3(5.8, 13.5, 33.1) * 1e-3; // km^-1
		}

		static void Preetham99(AtmosphereProfile& profile)
		{
			Bruneton17(profile);

			constexpr double pi = glm::pi<double>();
			double n = 1.0003; // index of refraction of air
			double N = 2.545e25; // number of molecules per unit volume
			double p_n = 0.035; // depolarization factor
			double kRayleigh =
				((8.0 * glm::pow(pi, 3.0) * glm::pow((n * n - 1.0), 2.0)) * (6.0 + 3.0 * p_n)) // Why [Bruneton08] 2.1 (1) omit the right half?
				/ // -----------------------------------------------------------------------------------------
				((3.0 * N) * (6.0 - 7.0 * p_n));
			profile.mRayleighScatteringCoefficient = kRayleigh / glm::pow(AtmosphereProfile::kLambda, glm::dvec3(4.0));
			profile.mRayleighScatteringCoefficient *= 1e3; // km^-1 <- m^-1

			// [NOTE]
			// Total Scattering Coefficient = Integral of Angular Scattering Coefficient in all directions
			// Angular Scattering Coefficient = Total Scattering Coefficient * (1 + cos(theta)^2) * 3.0 / 2.0
		}

		static void Yusov13(AtmosphereProfile& profile)
		{
			Preetham99(profile);

			static constexpr double kRayleighScaleHeight = 7.994;

			static constexpr float kDummy = 0.0f;
			profile.mRayleighDensityProfile.mLayer0 = { kDummy, kDummy, kDummy, kDummy, kDummy };
			profile.mRayleighDensityProfile.mLayer1 = { kDummy, 1.0f, -1.0f / static_cast<float>(kRayleighScaleHeight), 0.0f, 0.0f };
		}

		static void Hillaire20(AtmosphereProfile& profile)
		{
			Bruneton17(profile);

			profile.mRayleighScatteringCoefficient = glm::dvec3(0.005802f, 0.013558f, 0.033100f); // km^-1
		}
	};
	bool mEnableRayleigh								= {};
	ShaderType::DensityProfile mRayleighDensityProfile	= {}; // km
	glm::dvec3 mRayleighScatteringCoefficient			= {}; // km^-1

	// Mie
	struct MieReference
	{
		static void Bruneton17(AtmosphereProfile& profile)
		{
			static constexpr double kMieScaleHeight = 1.2; // km

			// [Wikipedia] The Angstrom exponent is a parameter that describes how the optical thickness of an aerosol typically depends on the wavelength of the light.
			static constexpr double kMieAngstromAlpha = 0.0;
			static constexpr double kMieAngstromBeta = 5.328e-3;
			static constexpr double kMieSingleScatteringAlbedo = 0.9;

			// [TODO] Different from paper?
			profile.mMieExtinctionCoefficient = kMieAngstromBeta / kMieScaleHeight * glm::pow(profile.kLambda, glm::dvec3(-kMieAngstromAlpha)); // km^-1
			profile.mMieScatteringCoefficient = profile.mMieExtinctionCoefficient * kMieSingleScatteringAlbedo; // km^-1

			profile.mMiePhaseFunctionG = 0.8;

			// [Bruneton08] 2.1 (3), e^(-1/H_M) Density decrease exponentially
			static constexpr float kDummy = 0.0f;
			profile.mMieDensityProfile.mLayer0 = { kDummy, kDummy, kDummy, kDummy, kDummy };
			profile.mMieDensityProfile.mLayer1 = { kDummy, 1.0f, -1.0f / static_cast<float>(kMieScaleHeight), 0.0f, 0.0f };

			profile.mEnableMie = true;
		}

		static void Bruneton08(AtmosphereProfile& profile) // 2.1 (3), beta_M(0, lambda)
		{
			Bruneton17(profile);

			profile.mMieScatteringCoefficient = glm::dvec3(20.0, 20.0, 20.0) * 1e-3; // km^-1
			profile.mMieExtinctionCoefficient = profile.mMieScatteringCoefficient / 0.9; // km^-1

			profile.mMiePhaseFunctionG = 0.76;
		}

		static void Yusov13(AtmosphereProfile& profile)
		{
			Bruneton08(profile);

			profile.mMiePhaseFunctionG = 0.76;
		}

		static void Hillaire20(AtmosphereProfile& profile)
		{
			Bruneton17(profile);

			profile.mMieScatteringCoefficient = glm::dvec3(0.003996f, 0.003996f, 0.003996f); // km^-1
			profile.mMieExtinctionCoefficient = glm::dvec3(0.004440f, 0.004440f, 0.004440f); // km^-1

			profile.mMiePhaseFunctionG = 0.8;
		}
	};
	bool mEnableMie										= {};
	ShaderType::DensityProfile mMieDensityProfile		= {}; // km
	glm::dvec3 mMieScatteringCoefficient				= {}; // km^-1
	glm::dvec3 mMieExtinctionCoefficient				= {}; // km^-1
	double mMiePhaseFunctionG							= {};

	// Ozone 
	struct OzoneReference
	{
		static void UpdateDensityProfile(AtmosphereProfile& profile)
		{
			// Density increase linearly, then decrease linearly
			float ozone_bottom_altitude = static_cast<float>(profile.mOzoneBottomAltitude);
			float ozone_mid_altitude = static_cast<float>(profile.mOzoneMidAltitude);
			float ozone_top_altitude = static_cast<float>(profile.mOzoneTopAltitude);
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

		static void Bruneton17(AtmosphereProfile& profile)				
		{ 
			profile.mEnableOzone = true;

			profile.mOzoneBottomAltitude				= 10.0;
			profile.mOzoneMidAltitude					= 25.0;
			profile.mOzoneTopAltitude					= 40.0;

			constexpr double kDobsonUnit = 2.687e20; // m^-2
			constexpr double kMaxOzoneNumberDensity	= 300.0 * kDobsonUnit / 15000.0; // m^-2
			constexpr glm::dvec3 kOzoneCrossSection	= glm::dvec3(1.209e-25, 3.5e-25, 1.582e-26); // m^2
			profile.mOZoneAbsorptionCoefficient = kMaxOzoneNumberDensity * kOzoneCrossSection;
			profile.mOZoneAbsorptionCoefficient *= 1e3; // km^1 <- m^-1

			UpdateDensityProfile(profile);
		}

		static void Hillaire20(AtmosphereProfile& profile)
		{
			Bruneton17(profile);

			profile.mOZoneAbsorptionCoefficient = glm::dvec3(0.000650f, 0.001881f, 0.000085f); // km^-1
		}
	};
	bool mEnableOzone									= {};
	double mOzoneBottomAltitude							= {}; // km
	double mOzoneMidAltitude							= {}; // km
	double mOzoneTopAltitude							= {}; // km
	ShaderType::DensityProfile mOzoneDensityProfile		= {}; // km
	glm::dvec3 mOZoneAbsorptionCoefficient				= {}; // km^-1

	// Solar
	struct SolarIrradianceReference
	{
		static void Bruneton17Constant(AtmosphereProfile& profile)		
		{ 
			profile.mSolarIrradiance = glm::vec3(1.5f); 
		}

		static void Bruneton17(AtmosphereProfile& profile)
		{ 
			// http://rredc.nrel.gov/solar/spectra/am1.5/ASTMG173/ASTMG173.html
			profile.mSolarIrradiance = glm::vec3(1.474000f, 1.850400f, 1.911980f); 
		}

		static void Hillaire20(AtmosphereProfile& profile)
		{
			profile.mSolarIrradiance = glm::vec3(1.0f, 1.0f, 1.0f);
		}
	};
	glm::vec3 mSolarIrradiance							= {}; // kW/m^2. why is this W/m^2 in [Bruneton17]
	bool mShowSolarIrradianceAsLuminance				= false;
	bool mPrecomputeWithSolarIrradiance					= true;

	// [Note] Calculated based on Sun seen from Earth
	// https://sciencing.com/calculate-angular-diameter-sun-8592633.html
	// Angular Radius = Angular Diameter / 2.0 = arctan(Sun radius / Sun-Earth distance)
	double kSunAngularRadius							= 0.00935f / 2.0f; // Radian, from [Bruneton17] demo.cc

	// Encoding Config
	AtmosphereMuSEncodingMode mMuSEncodingMode			= AtmosphereMuSEncodingMode::Bruneton17;

	// Multiple Scattering
	glm::uint mScatteringOrder							= 4;

	// Ground
	struct GroundReference
	{
		static void Bruneton17(AtmosphereProfile& profile)
		{
			profile.mGroundAlbedo						= glm::vec3(0.1f);
		}

		static void Hillaire20(AtmosphereProfile& profile)
		{
			profile.mGroundAlbedo						= glm::vec3(0.0f);
		}
	};
	bool mAerialPerspective								= true;
	glm::vec3 mGroundAlbedo								= glm::vec3(0.1f);
	glm::vec3 mRuntimeGroundAlbedo						= glm::vec3(0.0f, 0.0f, 0.04f);

	// Unit
	bool mSceneInKilometer								= true; // Meter otherwise

	struct Preset
	{
		static void Bruneton17(AtmosphereProfile& profile)
		{
			GeometryReference::Bruneton17(profile);
			RayleighReference::Bruneton17(profile);
			MieReference::Bruneton17(profile);
			OzoneReference::Bruneton17(profile);
			SolarIrradianceReference::Bruneton17(profile);
			GroundReference::Bruneton17(profile);
		}

		static void Hillaire20(AtmosphereProfile& profile)
		{
			GeometryReference::Hillaire20(profile);
			RayleighReference::Hillaire20(profile);
			MieReference::Hillaire20(profile);
			OzoneReference::Hillaire20(profile);
			SolarIrradianceReference::Hillaire20(profile);
			GroundReference::Hillaire20(profile);
		}
	};

	// Default
	AtmosphereProfile()
	{
		switch (mMode)
		{
		case AtmosphereMode::Hillaire20:	Preset::Hillaire20(*this); break;
		default:							Preset::Bruneton17(*this); break;
		}		
	}
};

class PrecomputedAtmosphereScattering
{
public:
	void Initialize();
	void Finalize();
	void UpdateImGui();
	void Update();
	void Load();
	void Precompute();
	void Compute();

	void ComputeTransmittance();
	void ComputeDirectIrradiance();
	void ComputeSingleScattering();
	void ComputeScatteringDensity(glm::uint scattering_order);
	void ComputeIndirectIrradiance(glm::uint scattering_order);
	void AccumulateMultipleScattering();
	void ComputeMultipleScattering(glm::uint scattering_order);
	bool mRecomputeRequested = true;
	bool mRecomputeEveryFrame = false;

	void TransLUT();
	void NewMultiScatCS();
	void SkyViewLut();
	void CameraVolumes();
};

struct PrecomputedAtmosphereScatteringResources
{
	ComPtr<ID3D12Resource> mConstantUploadBuffer;
	void* mConstantUploadBufferPointer = nullptr;

	// [Bruneton17]
	Shader mComputeTransmittanceShader			= Shader().CSName(L"ComputeTransmittanceCS");
	Shader mComputeDirectIrradianceShader		= Shader().CSName(L"ComputeDirectIrradianceCS");
	Shader mComputeSingleScatteringShader		= Shader().CSName(L"ComputeSingleScatteringCS");
	Shader mComputeScatteringDensityShader		= Shader().CSName(L"ComputeScatteringDensityCS");
	Shader mComputeIndirectIrradianceShader		= Shader().CSName(L"ComputeIndirectIrradianceCS");
	Shader mComputeMultipleScatteringShader		= Shader().CSName(L"ComputeMultipleScatteringCS");
	
	Texture mTransmittanceTexture				= Texture().Width(256).Height(64).Format(DXGI_FORMAT_R32G32B32A32_FLOAT).Name("Transmittance").UIScale(1.0f);
	Texture mDeltaIrradianceTexture				= Texture().Width(64).Height(16).Format(DXGI_FORMAT_R32G32B32A32_FLOAT).Name("Delta Irradiance").UIScale(4.0f);
	Texture mIrradianceTexture					= Texture().Width(64).Height(16).Format(DXGI_FORMAT_R32G32B32A32_FLOAT).Name("Irradiance").UIScale(4.0f);	
	Texture mDeltaRayleighScatteringTexture		= Texture().Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Rayleigh Scattering");
	Texture mDeltaMieScatteringTexture			= Texture().Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Mie Scattering");
	Texture mScatteringTexture					= Texture().Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Scattering");
	Texture mDeltaScatteringDensityTexture		= Texture().Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Delta Scattering Density");

	glm::uint mSliceCount = 0; // Slice axis to use 3D texture as 4D storage

	// [Hillaire20]
	Shader mTransLUTShader						= Shader().CSName(L"TransLUT");
	Shader mNewMultiScatCSShader				= Shader().CSName(L"NewMultiScatCS");
	Shader mSkyViewLutShader					= Shader().CSName(L"SkyViewLut");
	Shader mCameraVolumesShader					= Shader().CSName(L"CameraVolumes");

	Texture mTransmittanceTex					= Texture().Width(256).Height(64).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Hillaire20.TransmittanceTex").UIScale(1.0f);
	Texture mMultiScattTex						= Texture().Width(32).Height(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Hillaire20.MultiScattTex").UIScale(8.0f);
	Texture mSkyViewLutTex						= Texture().Width(192).Height(108).Format(DXGI_FORMAT_R11G11B10_FLOAT).Name("Hillaire20.SkyViewLutTex").UIScale(256.0f / 192.0f);
	Texture mAtmosphereCameraScatteringVolume	= Texture().Width(32).Height(32).Depth(32).Format(DXGI_FORMAT_R16G16B16A16_FLOAT).Name("Hillaire20.AtmosphereCameraScatteringVolume").UIScale(8.0f);

	static void Bruneton17(PrecomputedAtmosphereScatteringResources& resource)
	{
		constexpr glm::uvec3 kDimension = glm::uvec3(256, 128, 32);

		resource.mDeltaRayleighScatteringTexture.Dimension(kDimension);
		resource.mDeltaMieScatteringTexture.Dimension(kDimension);
		resource.mScatteringTexture.Dimension(kDimension);
		resource.mDeltaScatteringDensityTexture.Dimension(kDimension);

		resource.mSliceCount = 8;
	}

	static void Yusov13(PrecomputedAtmosphereScatteringResources& resource)
	{
		constexpr glm::uvec3 kDimension = glm::uvec3(32, 128, 1024);

		resource.mDeltaRayleighScatteringTexture.Dimension(kDimension);
		resource.mDeltaMieScatteringTexture.Dimension(kDimension);
		resource.mScatteringTexture.Dimension(kDimension);
		resource.mDeltaScatteringDensityTexture.Dimension(kDimension);

		resource.mSliceCount = 16;
	}

	// Put shaders in array to use in loop (use std::span when C++20 is available)
	std::vector<Shader*> mShaders =
	{
		// [Bruneton17]
		&mComputeTransmittanceShader,
		&mComputeDirectIrradianceShader,
		&mComputeSingleScatteringShader,
		&mComputeScatteringDensityShader,
		&mComputeIndirectIrradianceShader,
		&mComputeMultipleScatteringShader,

		// [Hillaire20]
		&mTransLUTShader,
		&mNewMultiScatCSShader,
		&mSkyViewLutShader,
		&mCameraVolumesShader,
	};
	
	std::vector<Texture*> mTextures =
	{
		// [Bruneton17]
		&mTransmittanceTexture,
		&mDeltaIrradianceTexture,
		&mIrradianceTexture,
		&mDeltaRayleighScatteringTexture,
		&mDeltaMieScatteringTexture,
		&mScatteringTexture,
		&mDeltaScatteringDensityTexture,

		// [Hillaire20]
		&mTransmittanceTex,
		&mMultiScattTex,
		&mSkyViewLutTex,
		&mAtmosphereCameraScatteringVolume
	};

	PrecomputedAtmosphereScatteringResources()
	{
		Bruneton17(*this);
		// Yusov13(*this);
	}
};

extern AtmosphereProfile gAtmosphereProfile;
extern PrecomputedAtmosphereScattering gPrecomputedAtmosphereScattering;
extern PrecomputedAtmosphereScatteringResources gPrecomputedAtmosphereScatteringResources;