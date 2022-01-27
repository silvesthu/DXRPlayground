// Code shared between HLSL and C++

// TODO: How to calculate RGB luminance of sun ?
// https://en.wikipedia.org/wiki/Luminous_efficacy
// https://en.wikipedia.org/wiki/Sunlight#Measurement
static const float kSunLuminousEfficacy = 93.0f; // lm/W
static const float kPreExposure = 1.0e-4f;
static const float kEmissionScale = 1.0e4f;
inline float3 RadianceToLuminance(float3 inRadiance)
{
	// https://en.wikipedia.org/wiki/Luminous_efficacy
	// https://en.wikipedia.org/wiki/Sunlight#Measurement

	float kW_to_W = 1000.0;
	float3 luminance = inRadiance * kW_to_W * kSunLuminousEfficacy * kPreExposure;
	return luminance;
}

enum class DebugMode : uint
{
	None = 0,

	_Newline0,

	Barycentrics,
	Vertex,
	Normal,

	_Newline1,

	Albedo,
	Reflectance,
	Emission,
	Roughness,

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
	RaymarchAtmosphereOnly,

	_Newline0,

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

	uint						mOutputLuminance		CONSTANT_DEFAULT(0);
	float						mEV100					CONSTANT_DEFAULT(16.0f);
	ToneMappingMode				mToneMappingMode		CONSTANT_DEFAULT(ToneMappingMode::Knarkowicz);
	float						_0						CONSTANT_DEFAULT(0.0f);
	
	float						mSunLuminanceScale		CONSTANT_DEFAULT(1.0f);
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
    float3						mAlbedo					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
    float3						mReflectance			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
    float3						mEmission				CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
    float						mRoughness				CONSTANT_DEFAULT(0.0f);

    uint						mIndexOffset			CONSTANT_DEFAULT(0);
    uint						mVertexOffset			CONSTANT_DEFAULT(0);
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

	float3						mPad5;
	uint						mPrecomputeWithSolarIrradiance	CONSTANT_DEFAULT(0);

	float3						mGroundAlbedo			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mAerialPerspective		CONSTANT_DEFAULT(0);

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

	struct CloudRaymarch
	{
		uint					mSampleCount			CONSTANT_DEFAULT(0);
		uint					mLightSampleCount		CONSTANT_DEFAULT(0);
		float					mLightSampleLength		CONSTANT_DEFAULT(0);
		float					mPad;
	};
	CloudRaymarch				mRaymarch;

	struct CloudGeometry
	{
		float					mStrato					CONSTANT_DEFAULT(0);
		float					mAlto					CONSTANT_DEFAULT(0);
		float					mCirro					CONSTANT_DEFAULT(0);
		float					mPad;
	};
	CloudGeometry				mGeometry;

	struct CloudShapeNoise
	{
		float3					mOffset					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
		float					mPad3;

		float					mFrequency				CONSTANT_DEFAULT(0);
		float					mPower					CONSTANT_DEFAULT(0);
		float					mScale					CONSTANT_DEFAULT(0);
		float					mPad4;
	};
	CloudShapeNoise				mShapeNoise;
};

struct DDGIConstants
{
	float4						mData;
};