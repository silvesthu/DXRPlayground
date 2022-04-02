// Code shared between HLSL and C++
#ifndef __INCLUDE_SHARED_INL__
#define __INCLUDE_SHARED_INL__

static const float MATH_PI						= 3.1415926535897932384626433832795f;
static const float kPreExposure					= 1.0e-4f;	// Pre-exposure to improve float point precision

// https://en.wikipedia.org/wiki/Luminous_efficacy
// https://en.wikipedia.org/wiki/Sunlight#Measurement
static const float kSolarLuminousEfficacy		= 93.0f; // lm/W
static const float kKW2W						= 1000.0f;
static const float kSolarKW2LM					= kKW2W * kSolarLuminousEfficacy;
static const float kSolarLM2KW					= 1.0f / kSolarKW2LM;

enum class MaterialType : uint
{
	None = 0,

	Diffuse,
	RoughConductor,

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

enum class DebugInstanceMode : uint
{
	None = 0,

	Barycentrics,
	Mirror,

	Count
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
	float4						mCameraPosition			CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 0.0f));
	float4						mCameraDirection		CONSTANT_DEFAULT(float4(0.0f, 0.0f, 1.0f, 0.0f));
	float4						mCameraRightExtend		CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));
	float4						mCameraUpExtend			CONSTANT_DEFAULT(float4(0.0f, 1.0f, 0.0f, 0.0f));

	uint						mReserved				CONSTANT_DEFAULT(0);
	float						mEV100					CONSTANT_DEFAULT(16.0f);
	ToneMappingMode				mToneMappingMode		CONSTANT_DEFAULT(ToneMappingMode::Knarkowicz);
	float						_0						CONSTANT_DEFAULT(0.0f);
	
	float						mSolarLuminanceScale	CONSTANT_DEFAULT(1.0f);
	float						mSunAzimuth				CONSTANT_DEFAULT(0);
	float						mSunZenith				CONSTANT_DEFAULT(MATH_PI / 4.0f);
	float						mTime					CONSTANT_DEFAULT(0);

	float4						mSunDirection			CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));

	DebugMode					mDebugMode				CONSTANT_DEFAULT(DebugMode::None);
	DebugInstanceMode			mDebugInstanceMode		CONSTANT_DEFAULT(DebugInstanceMode::None);
	uint						mDebugInstanceIndex		CONSTANT_DEFAULT(0);
	uint						_1						CONSTANT_DEFAULT(0);

	RecursionMode				mRecursionMode			CONSTANT_DEFAULT(RecursionMode::RussianRoulette);
	uint						mRecursionCountMax		CONSTANT_DEFAULT(4);
	uint						mFrameIndex				CONSTANT_DEFAULT(0);
	uint						mAccumulationFrameCount CONSTANT_DEFAULT(1);

	uint2						mDebugCoord				CONSTANT_DEFAULT(uint2(0, 0));
	uint						mReset					CONSTANT_DEFAULT(0);
	uint						_2						CONSTANT_DEFAULT(0);
};

struct InstanceData
{
	MaterialType				mMaterialType			CONSTANT_DEFAULT(MaterialType::None);
	float3						_0						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));

    float3						mAlbedo					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mOpacity				CONSTANT_DEFAULT(1.0f);

    float3						mReflectance			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mRoughnessAlpha			CONSTANT_DEFAULT(0.0f);

    float3						mEmission				CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						_1						CONSTANT_DEFAULT(0);

	float3						mTransmittance			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						_2						CONSTANT_DEFAULT(0);
	
	float3						mIOR					CONSTANT_DEFAULT(float3(1.0f, 1.0f, 1.0f));
	float						_3						CONSTANT_DEFAULT(0);

    uint						mIndexOffset			CONSTANT_DEFAULT(0);
    uint						mVertexOffset			CONSTANT_DEFAULT(0);
	float						_4						CONSTANT_DEFAULT(0);
	float						_5						CONSTANT_DEFAULT(0);
};

struct RayPayload
{
	uint						mRandomState;

	float3						mThroughput;     // [0, 1]		Accumulated throughput
	float3						mEmission;       // [0, +inf]	Accumulated emission

	float3						mPosition;
	float3						mReflectionDirection;

	bool						mDone;
};

struct ShadowPayload
{
	bool						mHit;
};

struct DensityProfileLayer
{
	float 						mWidth					CONSTANT_DEFAULT(0);
	float 						mExpTerm				CONSTANT_DEFAULT(0);
	float 						mExpScale				CONSTANT_DEFAULT(0);
	float 						mLinearTerm				CONSTANT_DEFAULT(0);

	float 						mConstantTerm			CONSTANT_DEFAULT(0);
	float3 						mPad;
};

struct DensityProfile
{
	DensityProfileLayer			mLayer0;
	DensityProfileLayer			mLayer1;
};

struct AtmosphereConstants
{
	float						mBottomRadius			CONSTANT_DEFAULT(0);
	float						mTopRadius				CONSTANT_DEFAULT(0);
	float						mSceneScale				CONSTANT_DEFAULT(0);
	uint						mPad0					CONSTANT_DEFAULT(0);

	AtmosphereMode				mMode					CONSTANT_DEFAULT(AtmosphereMode::Bruneton17);
	uint						mSliceCount				CONSTANT_DEFAULT(0);
	AtmosphereMuSEncodingMode	mMuSEncodingMode		CONSTANT_DEFAULT(AtmosphereMuSEncodingMode::Bruneton17);
	uint						mPadFlags;

	float4						mConstantColor			CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 1.0));

	float3						mRayleighScattering		CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mPad1;
	float3						mRayleighExtinction		CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mPad2;
	DensityProfile				mRayleighDensity;

	float3						mMieScattering			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mMiePhaseFunctionG		CONSTANT_DEFAULT(0);
	float3						mMieExtinction			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mPad3;
	DensityProfile				mMieDensity;

	float3						mOzoneExtinction		CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mPad4;
	DensityProfile				mOzoneDensity;

	float3						mSolarIrradiance		CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mSunAngularRadius		CONSTANT_DEFAULT(0);

	uint						mHillaire20SkyViewInLuminance		CONSTANT_DEFAULT(0);
	uint						mWilkie21SkyViewSplitScreen		CONSTANT_DEFAULT(0);
	float2						mPad5;

	float3						mGroundAlbedo			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	uint						mAerialPerspective		CONSTANT_DEFAULT(0);

	float3						mRuntimeGroundAlbedo	CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mPad6;
};

struct AtmosphereConstantsPerDraw
{
	uint						mScatteringOrder		CONSTANT_DEFAULT(0);
	uint						mPad0;
	uint						mPad1;
	uint						mPad2;
};

struct CloudConstants
{
	CloudMode					mMode					CONSTANT_DEFAULT(CloudMode::Noise);
	uint						mPad0;
	uint						mPad1;
	uint						mPad2;

	struct RayMarch
	{
		uint					mSampleCount			CONSTANT_DEFAULT(0);
		uint					mLightSampleCount		CONSTANT_DEFAULT(0);
		float					mLightSampleLength		CONSTANT_DEFAULT(0);
		float					mPad;
	};
	RayMarch					mRaymarch;

	struct Geometry
	{
		float					mStrato					CONSTANT_DEFAULT(0);
		float					mAlto					CONSTANT_DEFAULT(0);
		float					mCirro					CONSTANT_DEFAULT(0);
		float					mPad;
	};
	Geometry					mGeometry;

	struct ShapeNoise
	{
		float3					mOffset					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
		float					mPad3;

		float					mFrequency				CONSTANT_DEFAULT(0);
		float					mPower					CONSTANT_DEFAULT(0);
		float					mScale					CONSTANT_DEFAULT(0);
		float					mPad4;
	};
	ShapeNoise					mShapeNoise;
};

struct DDGIConstants
{
	float4						mData;
};

#endif // __INCLUDE_SHARED_INL__