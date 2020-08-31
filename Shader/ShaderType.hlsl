
static const uint sRecursionCountMax = 8;

struct PerFrame
{
	float4					mBackgroundColor		CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 1.0f));
	float4					mCameraPosition			CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 0.0f));
	float4					mCameraDirection		CONSTANT_DEFAULT(float4(0.0f, 0.0f, 1.0f, 0.0f));
	float4					mCameraRightExtend		CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));
	float4					mCameraUpExtend			CONSTANT_DEFAULT(float4(0.0f, 1.0f, 0.0f, 0.0f));
	
	BackgroundMode			mBackgroundMode			CONSTANT_DEFAULT(BackgroundMode::Atmosphere);
	float					mSunAzimuth				CONSTANT_DEFAULT(0);
	float					mSunZenith				CONSTANT_DEFAULT(MATH_PI / 2.0f);
	float					mPadding;
	float4					mSunDirection			CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));

	DebugMode				mDebugMode				CONSTANT_DEFAULT(DebugMode::None);
	DebugInstanceMode		mDebugInstanceMode		CONSTANT_DEFAULT(DebugInstanceMode::None);
	uint					mDebugInstanceIndex		CONSTANT_DEFAULT(0);
	ShadowMode				mShadowMode				CONSTANT_DEFAULT(ShadowMode::None);

	uint					mRecursionCountMax		CONSTANT_DEFAULT(sRecursionCountMax);
	uint					mFrameIndex				CONSTANT_DEFAULT(0);
	uint					mAccumulationFrameCount CONSTANT_DEFAULT(1);
	uint					mReset					CONSTANT_DEFAULT(0);

	uint2					mDebugCoord				CONSTANT_DEFAULT(uint2(0, 0));
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

struct RayPayload
{
    float3 mColor;
    uint mRandomState;
    uint mRecursionDepth;
};

struct ShadowPayload
{
    bool hit;
};