#pragma once
// Code shared between HLSL and C++

#ifdef __cplusplus

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using uint = glm::uint;
using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;

using int2 = glm::ivec2;
using int3 = glm::ivec3;
using int4 = glm::ivec4;

using float2x2 = glm::mat2x2;
using float2x3 = glm::mat2x3;
using float2x4 = glm::mat2x4;
using float3x2 = glm::mat3x2;
using float3x3 = glm::mat3x3;
using float3x4 = glm::mat3x4;
using float4x2 = glm::mat4x2;
using float4x3 = glm::mat4x3;
using float4x4 = glm::mat4x4;

#define CONSTANT_DEFAULT(x) = x
#define RETURN_AS_REFERENCE &
#define GET_COLUMN(x, i) x[i]
#define ENUM_CLASS_OPERATOR(T)									\
inline constexpr T operator&(T inContainer, T inValue)			\
{																\
	using U = std::underlying_type_t<T>;						\
	return (T)((U)inContainer & (U)inValue);					\
}																\
																\
inline constexpr T operator|(T inContainer, T inValue)			\
{																\
	using U = std::underlying_type_t<T>;						\
	return (T)((U)inContainer | (U)inValue);					\
}																\

#else

#define CONSTANT_DEFAULT(x)
#define RETURN_AS_REFERENCE
#define GET_COLUMN(x, i) transpose(x)[i]
#define ENUM_CLASS_OPERATOR(T)

#endif // __cplusplus

#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b
#define GENERATE_PAD_NAME CONCAT(mPad_, __LINE__)
#define GENERATE_NEW_LINE_NAME CONCAT(_Newline_, __LINE__)

static const uint kIndexCountPerTriangle		= 3;

static const float MATH_PI						= 3.1415926535897932384626433832795f;
static const float kPreExposure					= 1.0e-4f;	// Pre-exposure to improve float point precision

// https://en.wikipedia.org/wiki/Luminous_efficacy
// https://en.wikipedia.org/wiki/Sunlight#Measurement
static const float kSolarLuminousEfficacy		= 93.0f; // lm/W
static const float kKW2W						= 1000.0f;
static const float kSolarKW2LM					= kKW2W * kSolarLuminousEfficacy;
static const float kSolarLM2KW					= 1.0f / kSolarKW2LM;

static const uint kFrameInFlightCount			= 2;
static const uint kTimestampCount				= 1024;

enum class RTVDescriptorIndex : uint
{
	Invalid = 0,

	BackBuffer0,
	BackBuffer1,

	Count,
};

enum class DSVDescriptorIndex : uint
{
	Invalid = 0,

	ScreenDepth,

	Count,
};

enum class ViewDescriptorIndex : uint
{
	Invalid = 0,

	// [ImGui]
	ImGuiFont,
	ImGuiNull2D,
	ImGuiNull3D,

	// [Constants]
	Constants,

	// [Screen]
	ScreenColorSRV,
	ScreenColorUAV,
	ScreenDebugSRV,
	ScreenDebugUAV,
	ScreenDepthSRV,
	ScreenReservoirSRV,
	ScreenReservoirUAV,

	// [Debug]
	ConstantsCBV,
	PixelInspectionUAV,
	RayInspectionUAV,

	// [UVChecker]
	UVCheckerSRV,

	// [Generator]
	GeneratorSRV,
	GeneratorUAV,

	// [Misc]
	IESSRV,

	// [Raytrace] - [Input]
	RaytraceTLASSRV,
	RaytraceInstanceDataSRV,
	RaytraceIndicesSRV,
	RaytraceVerticesSRV,
	RaytraceNormalsSRV,
	RaytraceUVsSRV,
	RaytraceLightsSRV,
	RaytraceEncodedTriangleLightsUAV,

	// [Bruneton17]
	Bruneton17TransmittanceUAV,
	Bruneton17TransmittanceSRV,
	Bruneton17DeltaIrradianceUAV,
	Bruneton17DeltaIrradianceSRV,
	Bruneton17IrradianceUAV,
	Bruneton17IrradianceSRV,
	Bruneton17DeltaRayleighScatteringUAV,
	Bruneton17DeltaRayleighScatteringSRV,
	Bruneton17DeltaMieScatteringUAV,
	Bruneton17DeltaMieScatteringSRV,
	Bruneton17ScatteringUAV,
	Bruneton17ScatteringSRV,
	Bruneton17DeltaScatteringDensityUAV,
	Bruneton17DeltaScatteringDensitySRV,

	// [Hillaire20]
	Hillaire20TransmittanceTexUAV,
	Hillaire20TransmittanceTexSRV,
	Hillaire20MultiScattUAV,
	Hillaire20MultiScattSRV,
	Hillaire20SkyViewLutUAV,
	Hillaire20SkyViewLutSRV,
	Hillaire20AtmosphereCameraScatteringVolumeUAV,
	Hillaire20AtmosphereCameraScatteringVolumeSRV,

	// [Wilkie21]
	Wilkie21SkyViewUAV,
	Wilkie21SkyViewSRV,

	// [Validation] - [Hillaire20]
	ValidationHillaire20TransmittanceTexExpectedUAV,
	ValidationHillaire20TransmittanceTexExpectedSRV,
	ValidationHillaire20MultiScattExpectedUAV,
	ValidationHillaire20MultiScattExpectedSRV,
	ValidationHillaire20SkyViewLutExpectedUAV,
	ValidationHillaire20SkyViewLutExpectedSRV,
	ValidationHillaire20AtmosphereCameraScatteringVolumeExpectedUAV,
	ValidationHillaire20AtmosphereCameraScatteringVolumeExpectedSRV,
	ValidationHillaire20TransmittanceTexDiffUAV,
	ValidationHillaire20TransmittanceTexDiffSRV,
	ValidationHillaire20MultiScattDiffUAV,
	ValidationHillaire20MultiScattDiffSRV,
	ValidationHillaire20SkyViewLutDiffUAV,
	ValidationHillaire20SkyViewLutDiffSRV,
	ValidationHillaire20AtmosphereCameraScatteringVolumeDiffUAV,
	ValidationHillaire20AtmosphereCameraScatteringVolumeDiffSRV,

	// [Cloud]
	CloudShapeNoise2DUAV,
	CloudShapeNoise2DSRV,
	CloudErosionNoise2DUAV,
	CloudErosionNoise2DSRV,
	CloudShapeNoise3DUAV,
	CloudShapeNoise3DSRV,
	CloudErosionNoise3DUAV,
	CloudErosionNoise3DSRV,

	Count,

	SceneAutoSRV = Count,		// Indices started from this are allocated incrementally by Scene
};

enum class SamplerDescriptorIndex : uint
{
	BilinearClamp = 0,
	BilinearWrap,

	PointClamp,
	PointWrap,

	Count,
};

enum class RootParameterIndex : uint
{
	Constants = 0,				// ROOT_SIGNATURE_COMMON

	ConstantsPrepareLights = 1,	// ROOT_SIGNATURE_PREPARE_LIGHTS
	ConstantsDiff = 1,			// ROOT_SIGNATURE_DIFF
	ConstantsAtmosphere = 1,	// ROOT_SIGNATURE_ATMOSPHERE
};

enum class DebugMode : uint
{
	None = 0,

	GENERATE_NEW_LINE_NAME,

	Barycentrics,
	Position,
	Normal,
	UV,

	GENERATE_NEW_LINE_NAME,

	Albedo,
	Reflectance,
	RoughnessAlpha,
	Emission,

	GENERATE_NEW_LINE_NAME,

	Transmittance,
	InScattering,

	GENERATE_NEW_LINE_NAME,

	RecursionDepth,
	DebugValue,

	GENERATE_NEW_LINE_NAME,

	Cloud,

	Count
};

enum class PixelDebugMode : uint
{
	Manual,

	GENERATE_NEW_LINE_NAME,

	PositionWS,
	DirectionWS,
	InstanceID,

	GENERATE_NEW_LINE_NAME,

	// BSDF Sample
	BSDF__L,
	BSDF__V,
	BSDF__N,
	BSDF__H,
	BSDF__I,

	GENERATE_NEW_LINE_NAME,

	BSDF__D,
	BSDF__F,
	BSDF__G,
	BSDF__BSDF,
	BSDF__PDF,

	GENERATE_NEW_LINE_NAME,

	// Light Sample
	Light_L,
	Light_V,
	Light_N,
	Light_H,
	Light_I,

	GENERATE_NEW_LINE_NAME,

	Light_D,
	Light_F,
	Light_G,
	Light_BSDF,
	Light_PDF,

	GENERATE_NEW_LINE_NAME,

	LightIndex,
	RISWeight,

	GENERATE_NEW_LINE_NAME,

	Emission,
	Throughput,
	DiracDelta,

	GENERATE_NEW_LINE_NAME,

	RussianRoulette,
	EtaScale,

	GENERATE_NEW_LINE_NAME,

	BSDF_MIS,
	Light_MIS,

	Count
};

enum class OffsetMode : uint
{
	NoOffset = 0,
	HalfPixel,
	Random,

	Count
};

enum class SampleMode : uint
{
	SampleBSDF = 0,
	SampleLight,
	MIS,

	Count
};

enum class LightSourceMode : uint
{
	Emitter = 0,
	TriangleLights,
	Both,

	Count,
};

enum class LightSampleMode : uint
{
	Uniform = 0,
	RIS,

	Count,
};

enum class BSDF : uint
{
	Light = 0,

	Diffuse,				// https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_bsdfs.html#smooth-diffuse-material-diffuse

	Conductor,				// https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_bsdfs.html#smooth-conductor-conductor
	RoughConductor,			// https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_bsdfs.html#rough-conductor-material-roughconductor

	Dielectric,				// https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_bsdfs.html#smooth-dielectric-material-dielectric
	ThinDielectric,			// https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_bsdfs.html#thin-dielectric-material-thindielectric
	RoughDielectric,		// https://mitsuba.readthedocs.io/en/stable/src/generated/plugins_bsdfs.html#rough-dielectric-material-roughdielectric
	// Plastic	
	// Roughplastic

	glTF,					// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material

	Unsupported,			// Fallback to Diffuse

	Count
};

enum class DebugInstanceMode : uint
{
	None = 0,

	Barycentrics,
	Reflection,				// [TODO] Handle as dirac delta properly

	Count
};

enum class LightType : uint
{
	Sphere,
	Rectangle,

	Count,
};

enum class ToneMappingMode : uint
{
	Passthrough,
	Knarkowicz,

	Count
};

enum class AtmosphereMode : uint
{
	ConstantColor = 0,
	Raymarch,

	GENERATE_NEW_LINE_NAME,
	
	Bruneton17,
	Hillaire20,
	Wilkie21,

	Count
};

enum class AtmosphereMuSEncodingMode : uint
{
	Bruneton17 = 0,
	Bruneton08,
	Elek09,
	Yusov13,

	Count
};

enum class CloudMode  : uint
{
	None = 0,

	Noise,

	Count
};

struct TextureInfo
{
	uint						mTextureIndex : 16				CONSTANT_DEFAULT((uint)ViewDescriptorIndex::Invalid);
	uint						mSamplerIndex : 4				CONSTANT_DEFAULT((uint)SamplerDescriptorIndex::BilinearWrap);
	uint						mUnused	: 12					CONSTANT_DEFAULT(0);
};

struct InstanceData
{
	// [TODO] Split material

	BSDF						mBSDF							CONSTANT_DEFAULT(BSDF::Diffuse);
	uint						mTwoSided						CONSTANT_DEFAULT(0);
	float						mOpacity						CONSTANT_DEFAULT(1.0f);
	uint						mLightIndex						CONSTANT_DEFAULT(0);

	float						mRoughnessAlpha					CONSTANT_DEFAULT(0.0f);
	TextureInfo					mNormalTexture;
	float2						GENERATE_PAD_NAME				CONSTANT_DEFAULT(float2(0.0f, 0.0f));

    float3						mAlbedo							CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	TextureInfo					mAlbedoTexture;

    float3						mReflectance					CONSTANT_DEFAULT(float3(1.0f, 1.0f, 1.0f));
	TextureInfo					mReflectanceTexture;

	float3						mSpecularTransmittance			CONSTANT_DEFAULT(float3(1.0f, 1.0f, 1.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mEta							CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mMetallic						CONSTANT_DEFAULT(0);

	float3						mK								CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

    float3						mEmission						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	TextureInfo					mEmissionTexture;

	float4x4					mTransform						CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
	float4x4					mInverseTranspose				CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));

    uint						mVertexOffset					CONSTANT_DEFAULT(0);
	uint						mVertexCount					CONSTANT_DEFAULT(0);
	uint						mIndexOffset					CONSTANT_DEFAULT(0);
	uint						mIndexCount						CONSTANT_DEFAULT(0);
};

struct Light
{
	LightType					mType							CONSTANT_DEFAULT(LightType::Sphere);
	float2						mHalfExtends					CONSTANT_DEFAULT(float2(0.0f, 0.0f));
	uint						mInstanceID						CONSTANT_DEFAULT(0);

	float3						mPosition						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mTangent						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mBitangent						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mNormal							CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mEmission						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
};

// Based on RAB_LightInfo in RTXDI
struct EncodedTriangleLight
{
	float3						mPosition						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	uint						mExtends						CONSTANT_DEFAULT(0);											// 2x float16

	uint2						mEmission						CONSTANT_DEFAULT(uint2(0, 0));									// fp16x4
	uint						mTangent						CONSTANT_DEFAULT(0);											// oct-encoded
	uint						mBitangent						CONSTANT_DEFAULT(0);											// oct-encoded
};

struct RayState
{
	enum
	{
		None					= 0,
		Done					= 1,
	};

	void						Set(uint inBits)				{ mBits |= inBits; }
	void						Unset(uint inBits)				{ mBits &= ~inBits; }
	void						Reset(uint inBits)				{ mBits = inBits; }
	bool						IsSet(uint inBits)				{ return (mBits & inBits) != 0; }

	uint						mBits;
};

struct RayPayload
{
	float4						mData;
};

struct PathContext
{
	float3						mThroughput;					// [0, 1]		Accumulated throughput, [PBRT3] call it beta https://github.com/mmp/pbrt-v3/blob/master/src/integrators/path.cpp#L68
	float3						mEmission;						// [0, +inf]	Accumulated emission
	float						mEtaScale;

	float3						mLightEmission;					// [0, +inf]	Emission from light sample

	float						mPrevBSDFSamplePDF;
	bool						mPrevDiracDeltaDistribution;

	uint						mRandomState;
	uint						mRecursionDepth;
};

struct DensityProfileLayer
{
	float 						mWidth							CONSTANT_DEFAULT(0);
	float 						mExpTerm						CONSTANT_DEFAULT(0);
	float 						mExpScale						CONSTANT_DEFAULT(0);
	float 						mLinearTerm						CONSTANT_DEFAULT(0);

	float 						mConstantTerm					CONSTANT_DEFAULT(0);
	float3 						GENERATE_PAD_NAME;
};

struct DensityProfile
{
	DensityProfileLayer			mLayer0;
	DensityProfileLayer			mLayer1;
};

struct AtmosphereConstants
{
	float						mBottomRadius					CONSTANT_DEFAULT(0);
	float						mTopRadius						CONSTANT_DEFAULT(0);
	float						mSceneScale						CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	AtmosphereMode				mMode							CONSTANT_DEFAULT(AtmosphereMode::Bruneton17);
	uint						mSliceCount						CONSTANT_DEFAULT(0);
	AtmosphereMuSEncodingMode	mMuSEncodingMode				CONSTANT_DEFAULT(AtmosphereMuSEncodingMode::Bruneton17);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float4						mConstantColor					CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 1.0));

	float3						mRayleighScattering				CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float3						mRayleighExtinction				CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	DensityProfile				mRayleighDensity;

	float3						mMieScattering					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mMiePhaseFunctionG				CONSTANT_DEFAULT(0);
	float3						mMieExtinction					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	DensityProfile				mMieDensity;

	float3						mOzoneExtinction				CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	DensityProfile				mOzoneDensity;

	float3						mSolarIrradiance				CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mSunAngularRadius				CONSTANT_DEFAULT(0);

	uint						mHillaire20SkyViewInLuminance	CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mGroundAlbedo					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	uint						mAerialPerspective				CONSTANT_DEFAULT(0);

	float3						mRuntimeGroundAlbedo			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
};

struct CloudConstants
{
	CloudMode					mMode							CONSTANT_DEFAULT(CloudMode::None);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	struct RayMarch
	{
		uint					mSampleCount					CONSTANT_DEFAULT(0);
		uint					mLightSampleCount				CONSTANT_DEFAULT(0);
		float					mLightSampleLength				CONSTANT_DEFAULT(0);
		float					GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	};
	RayMarch					mRaymarch;

	struct Geometry
	{
		float					mStrato							CONSTANT_DEFAULT(0);
		float					mCirro							CONSTANT_DEFAULT(0);
		float					GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
		float					GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	};
	Geometry					mGeometry;

	struct ShapeNoise
	{
		float3					mOffset							CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
		float					GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

		float					mFrequency						CONSTANT_DEFAULT(0);
		float					mPower							CONSTANT_DEFAULT(0);
		float					mScale							CONSTANT_DEFAULT(0);
		float					GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	};
	ShapeNoise					mShapeNoise;
};

struct Constants
{
	// Right-handed Y-up
	float4 RETURN_AS_REFERENCE	CameraLeft()					{ return GET_COLUMN(mCameraTransform, 0); }
	float4 RETURN_AS_REFERENCE	CameraUp()						{ return GET_COLUMN(mCameraTransform, 1); }
	float4 RETURN_AS_REFERENCE	CameraFront()					{ return GET_COLUMN(mCameraTransform, 2); }
	float4 RETURN_AS_REFERENCE	CameraPosition()				{ return GET_COLUMN(mCameraTransform, 3); }
	float4x4					mCameraTransform				CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
	float4x4					mViewMatrix						CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
	float4x4					mProjectionMatrix				CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
	float4x4					mViewProjectionMatrix			CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
	float4x4					mInverseViewMatrix				CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
	float4x4					mInverseProjectionMatrix		CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
	float4x4					mInverseViewProjectionMatrix	CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));

	uint						mScreenWidth					CONSTANT_DEFAULT(0);
	uint						mScreenHeight					CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float						mEV100							CONSTANT_DEFAULT(16.0f);
	ToneMappingMode				mToneMappingMode				CONSTANT_DEFAULT(ToneMappingMode::Knarkowicz);
	float						mEmissionBoost					CONSTANT_DEFAULT(1);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float						mSolarLuminanceScale			CONSTANT_DEFAULT(1.0f);
	float						mSunAzimuth						CONSTANT_DEFAULT(0);
	float						mSunZenith						CONSTANT_DEFAULT(MATH_PI / 4.0f);
	float						mTime							CONSTANT_DEFAULT(0);

	OffsetMode					mOffsetMode						CONSTANT_DEFAULT(OffsetMode::HalfPixel);
	SampleMode					mSampleMode						CONSTANT_DEFAULT(SampleMode::MIS);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	uint						mLightCount						CONSTANT_DEFAULT(0);
	LightSourceMode				mLightSourceMode				CONSTANT_DEFAULT(LightSourceMode::Emitter);
	LightSampleMode				mLightSampleMode				CONSTANT_DEFAULT(LightSampleMode::Uniform);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float4						mSunDirection					CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));

	DebugMode					mDebugMode						CONSTANT_DEFAULT(DebugMode::None);
	DebugInstanceMode			mDebugInstanceMode				CONSTANT_DEFAULT(DebugInstanceMode::None);
	int							mDebugInstanceIndex				CONSTANT_DEFAULT(-1);
	int							mDebugLightIndex				CONSTANT_DEFAULT(-1);

	uint						mRecursionDepthCountMax			CONSTANT_DEFAULT(1);
	uint						mRussianRouletteDepth			CONSTANT_DEFAULT(1);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	uint						mCurrentFrameIndex				CONSTANT_DEFAULT(0);
	float						mCurrentFrameWeight				CONSTANT_DEFAULT(1);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	int2						mPixelDebugCoord				CONSTANT_DEFAULT(int2(-1, -1));
	int							mPixelDebugLightIndex			CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	PixelDebugMode				mPixelDebugMode					CONSTANT_DEFAULT(PixelDebugMode::Manual);
	int							mPixelDebugRecursion			CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	AtmosphereConstants			mAtmosphere;
	CloudConstants				mCloud;
};

struct PixelInspection
{
	static const uint			kArraySize						= 16;

	float4						mPixelValue						CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 0.0f));
	float4						mDebugValue						CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 0.0f));
	float4						mPixelValueArray[kArraySize];

	int							mPixelInstanceID				CONSTANT_DEFAULT(-1);
	uint3						GENERATE_PAD_NAME				CONSTANT_DEFAULT(uint3(0, 0, 0));
};

struct RayInspection
{
	static const uint			kArraySize						= 16;

	float4						mPositionWS[kArraySize];
	float4						mNormalWS[kArraySize];
};

struct LocalConstants
{
	uint						mShaderIndex					CONSTANT_DEFAULT(0);
	uint3						mPad							CONSTANT_DEFAULT(uint3(0, 0, 0));
};

#undef ENUM_CLASS_OPERATOR
#undef GET_COLUMN
#undef RETURN_AS_REFERENCE
#undef CONSTANT_DEFAULT
#undef CONCAT
#undef CONCAT_INNER
#undef GENERATE_PAD_NAME
