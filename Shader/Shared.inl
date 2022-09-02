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

#else

#define CONSTANT_DEFAULT(x)

#endif // __cplusplus

#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b
#define GENERATE_PAD_NAME CONCAT(mPad_, __LINE__)

static const float MATH_PI						= 3.1415926535897932384626433832795f;
static const float kPreExposure					= 1.0e-4f;	// Pre-exposure to improve float point precision

// https://en.wikipedia.org/wiki/Luminous_efficacy
// https://en.wikipedia.org/wiki/Sunlight#Measurement
static const float kSolarLuminousEfficacy		= 93.0f; // lm/W
static const float kKW2W						= 1000.0f;
static const float kSolarKW2LM					= kKW2W * kSolarLuminousEfficacy;
static const float kSolarLM2KW					= 1.0f / kSolarKW2LM;

// Persistent Descriptor Heap Entries
// Rather than allocate and pass index around, fixed slot is easier for debugging
enum class DescriptorIndex : uint
{
	NullSRV,
	NullUAV,

	NullCount,

	// [Constants]
	PerFrameConstants,

	// [Screen]
	ScreenColorUAV,
	ScreenColorSRV,
	ScreenDebugUAV,

	// [Raytrace] - [Input]
	RaytraceTLASSRV,
	RaytraceInstanceDataSRV,
	RaytraceIndicesSRV,
	RaytraceVerticesSRV,
	RaytraceNormalsSRV,
	RaytraceUVsSRV,
	RaytraceLightsSRV,

	// [Bruneton17]
	Bruneton17TransmittanceUAV,
	Bruneton17DeltaIrradianceUAV,
	Bruneton17IrradianceUAV,
	Bruneton17DeltaRayleighScatteringUAV,
	Bruneton17DeltaMieScatteringUAV,
	Bruneton17ScatteringUAV,
	Bruneton17DeltaScatteringDensityUAV,

	// [Hillaire20]
	Hillaire20TransmittanceTexUAV,
	Hillaire20MultiScattUAV,
	Hillaire20SkyViewLutUAV,
	Hillaire20AtmosphereCameraScatteringVolumeUAV,

	// [Wilkie21]
	Wilkie21SkyViewUAV,

	// [Validation] - [Hillaire20]
	ValidationHillaire20TransmittanceTexExpectedUAV,
	ValidationHillaire20MultiScattExpectedUAV,
	ValidationHillaire20SkyViewLutExpectedUAV,
	ValidationHillaire20AtmosphereCameraScatteringVolumeExpectedUAV,
	ValidationHillaire20TransmittanceTexDiffUAV,
	ValidationHillaire20MultiScattDiffUAV,
	ValidationHillaire20SkyViewLutDiffUAV,
	ValidationHillaire20AtmosphereCameraScatteringVolumeDiffUAV,

	// [Cloud]
	CloudShapeNoise2DUAV,
	CloudErosionNoise2DUAV,
	CloudShapeNoise3DUAV,
	CloudErosionNoise3DUAV,
	CloudShapeNoise3DSRV,
	CloudErosionNoise3DSRV,

	Count
};

enum class DebugMode : uint
{
	None = 0,

	_Newline0,

	Barycentrics,
	Vertex,
	Normal,
	UV,

	_Newline1,

	Albedo,
	Reflectance,
	RoughnessAlpha,
	Emission,

	_Newline2,

	Transmittance,
	InScattering,

	_Newline3,

	RecursionCount,
	RussianRouletteCount,

	Count
};

enum class RecursionMode : uint
{
	FixedCount = 0,
	RussianRoulette,

	Count
};

enum class MaterialType : uint
{
	Diffuse = 0,
	RoughConductor,

	// [TODO] Support more/unified materials. e.g. RoughPlastic

	Count
};

enum class DebugInstanceMode : uint
{
	None = 0,

	Barycentrics,
	Mirror,

	Count
};

enum class LightType : uint
{
	Sphere,

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
	Wilkie21,

	_Newline0,
	
	RaymarchAtmosphereOnly,
	Bruneton17,
	Hillaire20,

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

struct PerFrameConstants
{
	float4						mCameraPosition					CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 0.0f));
	float4						mCameraDirection				CONSTANT_DEFAULT(float4(0.0f, 0.0f, 1.0f, 0.0f));
	float4						mCameraRightExtend				CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));
	float4						mCameraUpExtend					CONSTANT_DEFAULT(float4(0.0f, 1.0f, 0.0f, 0.0f));

	float						mEV100							CONSTANT_DEFAULT(16.0f);
	ToneMappingMode				mToneMappingMode				CONSTANT_DEFAULT(ToneMappingMode::Knarkowicz);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	
	float						mSolarLuminanceScale			CONSTANT_DEFAULT(1.0f);
	float						mSunAzimuth						CONSTANT_DEFAULT(0);
	float						mSunZenith						CONSTANT_DEFAULT(MATH_PI / 4.0f);
	float						mTime							CONSTANT_DEFAULT(0);

	float4						mSunDirection					CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));

	DebugMode					mDebugMode						CONSTANT_DEFAULT(DebugMode::None);
	DebugInstanceMode			mDebugInstanceMode				CONSTANT_DEFAULT(DebugInstanceMode::None);
	int							mDebugInstanceIndex				CONSTANT_DEFAULT(-1);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	RecursionMode				mRecursionMode					CONSTANT_DEFAULT(RecursionMode::RussianRoulette);
	uint						mRecursionCountMax				CONSTANT_DEFAULT(4);
	uint						mFrameIndex						CONSTANT_DEFAULT(0);
	uint						mAccumulationFrameCount			CONSTANT_DEFAULT(1);

	uint2						mDebugCoord						CONSTANT_DEFAULT(uint2(0, 0));
	uint						mReset							CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
};

struct InstanceData
{
	// [TODO] Split material

	MaterialType				mMaterialType					CONSTANT_DEFAULT(MaterialType::Diffuse);
	float3						GENERATE_PAD_NAME				CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));

    float3						mAlbedo							CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mOpacity						CONSTANT_DEFAULT(1.0f);

    float3						mReflectance					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mRoughnessAlpha					CONSTANT_DEFAULT(0.0f);

    float3						mEmission						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mTransmittance					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	
	float3						mIOR							CONSTANT_DEFAULT(float3(1.0f, 1.0f, 1.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

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
	float3						GENERATE_PAD_NAME;

	float3						mPosition						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mRadius							CONSTANT_DEFAULT(0);
};

struct RayPayload
{
	float3						mThroughput;					// [0, 1]		Accumulated throughput
	float3						mEmission;						// [0, +inf]	Accumulated emission

	float3						mPosition;
	float3						mReflectionDirection;

	uint						mRandomState;
	bool						mDone;

	// Certain layout might cause driver crash on PSO generation
	// See https://github.com/silvesthu/DirectX-Graphics-Samples/commit/9822cb8142629515f3768d2c36ff6695dba04838
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
	uint						mWilkie21SkyViewSplitScreen		CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mGroundAlbedo					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	uint						mAerialPerspective				CONSTANT_DEFAULT(0);

	float3						mRuntimeGroundAlbedo			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
};

struct AtmosphereConstantsPerDraw
{
	uint						mScatteringOrder				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
};

struct CloudConstants
{
	CloudMode					mMode							CONSTANT_DEFAULT(CloudMode::Noise);
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
		float					mAlto							CONSTANT_DEFAULT(0);
		float					mCirro							CONSTANT_DEFAULT(0);
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

#undef CONSTANT_DEFAULT
#undef CONCAT
#undef CONCAT_INNER
#undef GENERATE_PAD_NAME
