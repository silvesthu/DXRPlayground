// Code shared between HLSL and C++

// https://en.wikipedia.org/wiki/Luminous_efficacy
// https://en.wikipedia.org/wiki/Sunlight#Measurement
static const float kSunLuminousEfficacy = 93.0; // lm/W

struct PerFrame
{
	float4					mCameraPosition			CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 0.0f));
	float4					mCameraDirection		CONSTANT_DEFAULT(float4(0.0f, 0.0f, 1.0f, 0.0f));
	float4					mCameraRightExtend		CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));
	float4					mCameraUpExtend			CONSTANT_DEFAULT(float4(0.0f, 1.0f, 0.0f, 0.0f));

	uint					mOutputLuminance		CONSTANT_DEFAULT(0);
	float					mEV100					CONSTANT_DEFAULT(16.0f);
	TonemapMode				mTonemapMode			CONSTANT_DEFAULT(TonemapMode::knarkowicz);
	float					_0						CONSTANT_DEFAULT(0.0f);
	
	float					mPadding				CONSTANT_DEFAULT(0);
	float					mSunAzimuth				CONSTANT_DEFAULT(0);
	float					mSunZenith				CONSTANT_DEFAULT(MATH_PI / 4.0f);
	float					mTime					CONSTANT_DEFAULT(0);

	float4					mSunDirection			CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));

	DebugMode				mDebugMode				CONSTANT_DEFAULT(DebugMode::None);
	DebugInstanceMode		mDebugInstanceMode		CONSTANT_DEFAULT(DebugInstanceMode::None);
	uint					mDebugInstanceIndex		CONSTANT_DEFAULT(0);
	uint					_1						CONSTANT_DEFAULT(0);

	RecursionMode			mRecursionMode			CONSTANT_DEFAULT(RecursionMode::RussianRoulette);
	uint					mRecursionCountMax		CONSTANT_DEFAULT(4);
	uint					mFrameIndex				CONSTANT_DEFAULT(0);
	uint					mAccumulationFrameCount CONSTANT_DEFAULT(1);

	uint2					mDebugCoord				CONSTANT_DEFAULT(uint2(0, 0));
	uint					mReset					CONSTANT_DEFAULT(0);
	uint					_2						CONSTANT_DEFAULT(0);
};

struct InstanceData
{
    float3					mAlbedo					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
    float3					mReflectance			CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
    float3					mEmission				CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
    float					mRoughness				CONSTANT_DEFAULT(0.0f);

    uint					mIndexOffset			CONSTANT_DEFAULT(0);
    uint					mVertexOffset			CONSTANT_DEFAULT(0);
};

struct HitInfo
{
	float3 mAlbedo;
	float3 mEmission;

	float3 mPosition;
	float3 mReflectionDirection;

	float mScatteringPDF;
	float mPDF;

	// Participating Media along the ray
	float3 mTransmittance;
	float3 mInScattering;

	bool mDone;
};

struct RayPayload
{
    uint mRandomState;

	float3 mThroughput; // [0, 1]		Accumulated throughput
	float3 mEmission;	// [0, +inf]	Accumulated emission
	
    float3 mPosition;
    float3 mReflectionDirection;

	bool mDone;
};

struct ShadowPayload
{
    bool mHit;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DensityProfileLayer 
{
	float mWidth									CONSTANT_DEFAULT(0);
	float mExpTerm									CONSTANT_DEFAULT(0);
	float mExpScale									CONSTANT_DEFAULT(0);
	float mLinearTerm								CONSTANT_DEFAULT(0);

	float mConstantTerm								CONSTANT_DEFAULT(0);
	float3 mPad;
};

#ifndef HLSL_AS_CPP
// Altitude -> Density
float GetLayerDensity(DensityProfileLayer layer, float altitude)
{
	float density = layer.mExpTerm * exp(layer.mExpScale * altitude) + layer.mLinearTerm * altitude + layer.mConstantTerm;
	return clamp(density, 0.0, 1.0);
}
#endif // HLSL_AS_CPP

struct DensityProfile 
{
	DensityProfileLayer mLayer0;
	DensityProfileLayer mLayer1;
};

#ifndef HLSL_AS_CPP
float GetProfileDensity(DensityProfile profile, float altitude)
{
	if (altitude < profile.mLayer0.mWidth)
		return GetLayerDensity(profile.mLayer0, altitude);
	else
		return GetLayerDensity(profile.mLayer1, altitude);
}
#endif // HLSL_AS_CPP

struct Atmosphere
{
	float						mBottomRadius			CONSTANT_DEFAULT(0);
	float						mTopRadius				CONSTANT_DEFAULT(0);
	float						mSceneScale				CONSTANT_DEFAULT(0);
	uint						mDensitySampleCount		CONSTANT_DEFAULT(0);

	AtmosphereMode				mMode					CONSTANT_DEFAULT(AtmosphereMode::PrecomputedAtmosphere);
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

struct AtmospherePerDraw
{
	uint						mScatteringOrder		CONSTANT_DEFAULT(0);
	uint						mPad0;
	uint						mPad1;
	uint						mPad2;
};

struct CloudRaymarch
{
	uint						mSampleCount			CONSTANT_DEFAULT(0);
	uint						mLightSampleCount		CONSTANT_DEFAULT(0);
	float						mLightSampleLength		CONSTANT_DEFAULT(0);
	float						mPad;
};

struct CloudGeometry
{
	float						mStrato					CONSTANT_DEFAULT(0);
	float						mAlto					CONSTANT_DEFAULT(0);
	float						mCirro					CONSTANT_DEFAULT(0);
	float						mPad;
};

struct CloudShapeNoise
{
	float3						mOffset					CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						mPad3;

	float						mFrequency				CONSTANT_DEFAULT(0);
	float						mPower					CONSTANT_DEFAULT(0);
	float						mScale					CONSTANT_DEFAULT(0);
	float						mPad4;
};

struct Cloud
{
	CloudMode					mMode					CONSTANT_DEFAULT(CloudMode::Noise);
	uint						mPad0;
	uint						mPad1;
	uint						mPad2;

	CloudRaymarch				mRaymarch;
	CloudGeometry				mGeometry;
	CloudShapeNoise				mShapeNoise;
};

struct DDGI
{
	float4						mData;
};