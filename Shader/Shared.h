#pragma once
// Code shared between HLSL and C++

#ifdef __cplusplus

#include <glm.h>	// glm
#include <bit>		// std::bit_cast

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
#define STATITC_ASSERT(x) static_assert(x)
#define COMPUTE_SHADER_INPUT uint3 inGroupThreadID, uint3 inGroupID, uint3 inDispatchThreadID, uint inGroupIndex

inline float asfloat(uint x) { return std::bit_cast<float>(x); }
inline uint asuint(float x) { return std::bit_cast<uint>(x); }

#else

#define CONSTANT_DEFAULT(x)
#define RETURN_AS_REFERENCE
#define GET_COLUMN(x, i) transpose(x)[i]
#define STATITC_ASSERT(x)
#define COMPUTE_SHADER_INPUT uint3 inGroupThreadID : SV_GroupThreadID, uint3 inGroupID : SV_GroupID, uint3 inDispatchThreadID : SV_DispatchThreadID, uint inGroupIndex : SV_GroupIndex

#endif // __cplusplus

#ifdef __SLANG__
#define MUTATING [mutating]
#else
#define MUTATING
#endif

#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b
#define GENERATE_PAD_NAME CONCAT(mPad_, __LINE__)
#define GENERATE_NEW_LINE_NAME CONCAT(_Newline_, __LINE__)

#include "EnumHelper.h"

static const uint kIndexCountPerTriangle		= 3;

static const float MATH_PI						= 3.1415926535897932384626433832795f;
static const float kPreExposure					= 1.0e-4f;	// Pre-exposure to improve float point precision

// https://en.wikipedia.org/wiki/Luminous_efficacy
// https://en.wikipedia.org/wiki/Sunlight#Measurement
static const float kSolarLuminousEfficacy		= 93.0f; // lm/W
static const float kKW2W						= 1000.0f;
static const float kSolarKW2LM					= kKW2W * kSolarLuminousEfficacy;
static const float kSolarLM2KW					= 1.0f / kSolarKW2LM;

static const int kFrameInFlightCount			= 2;

static const uint kSpatialHashSize				= 1024 * 1024;

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
	ConstantsCBV,

	// [Screen]
	ScreenColorSRV,
	ScreenColorUAV,
	ScreenDebugSRV,
	ScreenDebugUAV,
	ScreenDepthSRV,
	ScreenReservoirSRV,
	ScreenReservoirUAV,
	ScreenReadbackSRV,
	ScreenReadbackUAV,

	// [Debug]
	InspectDataUAV,

	// [UVChecker]
	UVCheckerSRV,

	// [Generator]
	GeneratedSRV,
	GeneratedUAV,

	// [BSDF]
	BRDFSliceSRV,
	BRDFSliceUAV,

	// [Noise]
	ShapeNoise3DSRV,
	ErosionNoise3DSRV,

	// [SpatialHash]
	SpatialHashUAV,
	SpatialDataUAV,

	// [ShaderPrint]
	ShaderPrintUAV,

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

	SceneAutoIndex = Count,		// Indices started from this are allocated incrementally by Scene
};

enum class ClearMode : uint
{
	Invalid		= 0,

	UInt4,
	Float4,

	Count,
};

#define ROOT_CONSTANTS_NUM_32BIT				8
#define ROOT_CONSTANTS_REGISTER					0
#define ROOT_CBV_REGISTER						1

#define COMMON_ROOT_SIGNATURE_REGISTER_SPACE	0
#define LOCAL_ROOT_SIGNATURE_REGISTER_SPACE		100

#define REGISTER_CBV_CONCAT(R, S)				register(b ## R, space ## S)
#define REGISTER_CBV(R, S)						REGISTER_CBV_CONCAT(R, S)
#define REGISTER_SRV_CONCAT(R, S)				register(t ## R, space ## S)
#define REGISTER_SRV(R, S)						REGISTER_SRV_CONCAT(R, S)
#define REGISTER_UAV_CONCAT(R, S)				register(u ## R, space ## S)
#define REGISTER_UAV(R, S)						REGISTER_UAV_CONCAT(R, S)

// https://developer.nvidia.com/blog/improve-shader-performance-and-in-game-frame-rates-with-shader-execution-reordering/
#ifdef __cplusplus
#define NV_SHADER_EXTN_SLOT						999999
#define NV_SHADER_EXTN_REGISTER_SPACE			999999
#else
#define NV_SHADER_EXTN_SLOT						u999999
#define NV_SHADER_EXTN_REGISTER_SPACE			space999999
#endif // __cplusplus

enum class SamplerDescriptorIndex : uint
{
	BilinearClamp = 0,
	BilinearWrap,

	PointClamp,
	PointWrap,

	Count,
};

enum class VisualizeMode : uint
{
	None = 0,
	PrimitiveIndex,
	ClusterID,

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
	RandomState,

	GENERATE_NEW_LINE_NAME,

	SpatialHash,
	SpatialData,

	GENERATE_NEW_LINE_NAME,

	Inspect,

	Count
};

enum class InspectMode : uint
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
	BSDF__Lobe,

	GENERATE_NEW_LINE_NAME,

	BSDF__D,
	BSDF__F,
	BSDF__G,
	BSDF__BSDF,
	BSDF__PDF,

	GENERATE_NEW_LINE_NAME,

	Eta,
	DiracDelta,

	GENERATE_NEW_LINE_NAME,
	
	MediumInstanceID,
	MediumExtinction,
	MediumFreeFlight,

	GENERATE_NEW_LINE_NAME,

	// Light Sample
	Light_L,
	Light_V,
	Light_N,
	Light_H,
	Light_Lobe,

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

	Throughput,
	EtaScale,
	Emission,
	RussianRoulette,

	GENERATE_NEW_LINE_NAME,

	MIS_BSDF,
	MIS_LIGHT,

	GENERATE_NEW_LINE_NAME,

	RIS_SAMPLE,
	RIS_SUM,
	
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
	BSDF = 0,	// BSDF sample only
	Light,		// Light sample (NEE) only
	MIS,		// MIS

	Count
};

enum class LightSampleMode : uint
{
	Uniform = 0,
	ReSTIR,

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

	pbrMetallicRoughness,	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-pbrmetallicroughness
	pbrSpecularGlossiness,	// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness/README.md

	Unsupported,			// Fallback to Diffuse

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

enum class CloudMode : uint
{
	None = 0,

	Noise,

	Count
};

enum class ShaderPrintEntryType : uint
{
	Nop = 0,
	String,
	Float1,
	Float2,
	Float3,
	Float4,
	UInt1,
	UInt2,
	UInt3,
	UInt4,
};

BEGIN_ENUM_FLAG(DebugFlag)
	None						= 0,
	UpdateInspectRay			= 1 << 0,
END_ENUM_FLAG(DebugFlag)
ENABLE_UINT_ENUM_BITWISE_OPERATORS(DebugFlag)

struct TextureInfo
{
	uint						mTextureIndex : 16				CONSTANT_DEFAULT((uint)ViewDescriptorIndex::Invalid);
	uint						mSamplerIndex : 4				CONSTANT_DEFAULT((uint)SamplerDescriptorIndex::BilinearWrap);
	uint						mUnused	: 12					CONSTANT_DEFAULT(0);
};
STATITC_ASSERT(sizeof(TextureInfo) == sizeof(float) * 1);

struct NanoVDBInfo
{
	uint						mBufferIndex					CONSTANT_DEFAULT((uint)ViewDescriptorIndex::Invalid);
	uint						mTextureIndex					CONSTANT_DEFAULT((uint)ViewDescriptorIndex::Invalid);
	uint2						mPad							CONSTANT_DEFAULT(uint2(0, 0));
	
	uint3						mOffset							CONSTANT_DEFAULT(uint3(0, 0, 0));
	float						mMinimum						CONSTANT_DEFAULT(0);

	uint3						mSize							CONSTANT_DEFAULT(uint3(0, 0, 0));
	float						mMaximum						CONSTANT_DEFAULT(0);
};
STATITC_ASSERT(sizeof(NanoVDBInfo) == sizeof(float) * 12);

enum : uint
{
	InvalidInstanceID = 0xffff,
};

struct InstanceFlag
{
	uint						mTwoSided : 1					CONSTANT_DEFAULT(0);
	uint						mNormal : 1						CONSTANT_DEFAULT(0);
	uint						mUV : 1							CONSTANT_DEFAULT(0);

	uint						mInstanceMask : 8				CONSTANT_DEFAULT(0xff);

	uint						mPad : 21						CONSTANT_DEFAULT(0);
};
STATITC_ASSERT(sizeof(InstanceFlag) == sizeof(float) * 1);

struct InstanceData
{
	BSDF						mBSDF							CONSTANT_DEFAULT(BSDF::Diffuse);
	InstanceFlag				mFlags;
	float						mOpacity						CONSTANT_DEFAULT(1.0f);
	uint						mLightIndex						CONSTANT_DEFAULT(0);

	float						mRoughnessAlpha					CONSTANT_DEFAULT(0.0f);
	TextureInfo					mNormalTexture;
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

    float3						mAlbedo							CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	TextureInfo					mAlbedoTexture;

    float3						mReflectance					CONSTANT_DEFAULT(float3(1.0f, 1.0f, 1.0f));
	TextureInfo					mReflectanceTexture;

	float3						mSpecularTransmittance			CONSTANT_DEFAULT(float3(1.0f, 1.0f, 1.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mEta							CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float3						mK								CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

    float3						mEmission						CONSTANT_DEFAULT(float3(0.0f, 0.0f, 0.0f));
	TextureInfo					mEmissionTexture;

	float3						mMediumAlbedo					CONSTANT_DEFAULT(float3(0.75f, 0.75f, 0.75f));
	uint						mMedium							CONSTANT_DEFAULT(0);

	float3						mMediumSigmaT					CONSTANT_DEFAULT(float3(1.0f, 1.0f, 1.0f));
	float						mMediumPhase					CONSTANT_DEFAULT(0.5f);

	NanoVDBInfo					mMediumNanoVBD;

	float4x4					mTransform						CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));
	float4x4					mInverseTranspose				CONSTANT_DEFAULT(float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1));

    uint						mVertexOffset					CONSTANT_DEFAULT(0);
	uint						mVertexCount					CONSTANT_DEFAULT(0);
	uint						mIndexOffset					CONSTANT_DEFAULT(0);
	uint						mIndexCount						CONSTANT_DEFAULT(0);

	uint						mLSSVertexOffset				CONSTANT_DEFAULT(0);
	uint						mLSSVertexCount					CONSTANT_DEFAULT(0);
	uint						mLSSIndexOffset					CONSTANT_DEFAULT(0);
	uint						mLSSIndexCount					CONSTANT_DEFAULT(0);
	uint						mLSSRadiusOffset				CONSTANT_DEFAULT(0);
	uint						mLSSRadiusCount					CONSTANT_DEFAULT(0);
	uint						mClusterMeshletBufferIndex		CONSTANT_DEFAULT(0);
	uint						mClusterIndexBufferIndex		CONSTANT_DEFAULT(0);
};
STATITC_ASSERT(sizeof(InstanceData) % sizeof(glm::vec4) == 0);

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
STATITC_ASSERT(sizeof(Light) % sizeof(glm::vec4) == 0);

struct RayState
{
	enum
	{
		None					= 0,
		Done					= 1,
	};

	MUTATING void				Set(uint inBits)				{ mBits |= inBits; }
	MUTATING void				Unset(uint inBits)				{ mBits &= ~inBits; }
	MUTATING void				Reset(uint inBits)				{ mBits = inBits; }
	bool						IsSet(uint inBits)				{ return (mBits & inBits) != 0; }

	uint						mBits;
};

struct RayPayload
{
	float4						mData;
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
	float4						mConstantColor					CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 1.0));

	float						mBottomRadius					CONSTANT_DEFAULT(0);
	float						mTopRadius						CONSTANT_DEFAULT(0);
	float						mSceneScale						CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	AtmosphereMode				mMode							CONSTANT_DEFAULT(AtmosphereMode::Bruneton17);
	uint						mSliceCount						CONSTANT_DEFAULT(0);
	AtmosphereMuSEncodingMode	mMuSEncodingMode				CONSTANT_DEFAULT(AtmosphereMuSEncodingMode::Bruneton17);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

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
	float						mSunDiskLuminanceScale			CONSTANT_DEFAULT(1.0f);
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

struct BRDFExplorerConstants
{
	float3						mBaseColor						CONSTANT_DEFAULT(float3(0.82f, 0.67f, 0.16f));
	float						mMetallic						CONSTANT_DEFAULT(0.0f);

	float						mSubsurface						CONSTANT_DEFAULT(0.0f);
	float						mSpecular						CONSTANT_DEFAULT(0.5f);
	float						mRoughness						CONSTANT_DEFAULT(0.5f);
	float						mSpecularTint					CONSTANT_DEFAULT(0.0f);

	float						mAnisotropic					CONSTANT_DEFAULT(0.0f);
	float						mSheen							CONSTANT_DEFAULT(0.0f);
	float						mSheenTint						CONSTANT_DEFAULT(0.5f);
	float						mClearcoat						CONSTANT_DEFAULT(0.0f);

	float						mClearcoatGloss					CONSTANT_DEFAULT(1.0f);
	float						mPhiD							CONSTANT_DEFAULT(MATH_PI / 2.0);
	float						mGamma							CONSTANT_DEFAULT(1.0f);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
};

struct SpatialCacheConstants
{
	uint						mFrameActive					CONSTANT_DEFAULT(0);
	uint						mFrameCount						CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
};

BEGIN_ENUM_FLAG(ReSTIRFlag)
	None						= 0,
	TemporalReuse				= 1 << 0,
	SpatialReuse				= 1 << 1,
END_ENUM_FLAG(ReSTIRFlag)
ENABLE_UINT_ENUM_BITWISE_OPERATORS(ReSTIRFlag)

struct ReSTIRConstants
{
	uint						mTemporalFrameIndex				CONSTANT_DEFAULT(0);
	uint						mInitialSampleCount				CONSTANT_DEFAULT(1);
	ENUM_FLAG_TYPE(ReSTIRFlag)	mFlags							CONSTANT_DEFAULT(ReSTIRFlag::None);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
};

struct RootConstants
{
	uint4						mData0							CONSTANT_DEFAULT(uint4(0, 0, 0, 0));
	uint4						mData1							CONSTANT_DEFAULT(uint4(0, 0, 0, 0));
};
STATITC_ASSERT(sizeof(RootConstants) == ROOT_CONSTANTS_NUM_32BIT * 4);

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
	uint						mFrameIndex						CONSTANT_DEFAULT(0);
	float						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float						mEV100							CONSTANT_DEFAULT(16.0f);
	ToneMappingMode				mToneMappingMode				CONSTANT_DEFAULT(ToneMappingMode::Knarkowicz);
	float						mEmissionBoost					CONSTANT_DEFAULT(1);
	float						mDensityBoost					CONSTANT_DEFAULT(1);

	float						mSolarLuminanceScale			CONSTANT_DEFAULT(1.0f);
	float						mSunAzimuth						CONSTANT_DEFAULT(0);
	float						mSunZenith						CONSTANT_DEFAULT(MATH_PI / 4.0f);
	float						mTime							CONSTANT_DEFAULT(0);

	OffsetMode					mOffsetMode						CONSTANT_DEFAULT(OffsetMode::HalfPixel);
	SampleMode					mSampleMode						CONSTANT_DEFAULT(SampleMode::MIS);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	uint						mLightCount						CONSTANT_DEFAULT(0);
	LightSampleMode				mLightSampleMode				CONSTANT_DEFAULT(LightSampleMode::Uniform);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	float4						mSunDirection					CONSTANT_DEFAULT(float4(1.0f, 0.0f, 0.0f, 0.0f));

	VisualizeMode				mVisualizeMode					CONSTANT_DEFAULT(VisualizeMode::None);
	int							mDebugInstanceIndex				CONSTANT_DEFAULT(-1);
	int							mDebugLightIndex				CONSTANT_DEFAULT(-1);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	uint						mRecursionDepthCountMax			CONSTANT_DEFAULT(1);
	uint						mRussianRouletteDepth			CONSTANT_DEFAULT(1);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	int							mCurrentFrameIndex				CONSTANT_DEFAULT(0);
	float						mCurrentFrameWeight				CONSTANT_DEFAULT(1);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	int							mSequenceEnabled				CONSTANT_DEFAULT(0);
	int							mSequenceFrameIndex				CONSTANT_DEFAULT(0);
	int							mSequenceFrameCount				CONSTANT_DEFAULT(192);
	float						mSequenceFrameRatio				CONSTANT_DEFAULT(0.0f);

	int2						mPixelDebugCoord				CONSTANT_DEFAULT(int2(-1, -1));
	int							mPixelDebugLightIndex			CONSTANT_DEFAULT(0);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	InspectMode					mInspectMode					CONSTANT_DEFAULT(InspectMode::Manual);
	int							mDebugRecursion					CONSTANT_DEFAULT(0);
	ENUM_FLAG_TYPE(DebugFlag)	mDebugFlag						CONSTANT_DEFAULT(DebugFlag::None);
	uint						GENERATE_PAD_NAME				CONSTANT_DEFAULT(0);

	AtmosphereConstants			mAtmosphere;
	CloudConstants				mCloud;
	BRDFExplorerConstants		mBRDFExplorer;
	SpatialCacheConstants		mSpatialCache;
	ReSTIRConstants				mReSTIR;
};

struct InspectData
{
	float4						mScreenColor					CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 0.0f));
	float4						mScreenDebug					CONSTANT_DEFAULT(float4(0.0f, 0.0f, 0.0f, 0.0f));

	int							mPixelInstanceID				CONSTANT_DEFAULT(-1);
	uint3						GENERATE_PAD_NAME				CONSTANT_DEFAULT(uint3(0, 0, 0));

	static const uint			kPathLength = 16;
	float4						mValue[kPathLength];
	float4						mPositionWS[kPathLength];
	float4						mNormalWS[kPathLength];
	float4						mLightPositionWS[kPathLength];
};

struct LocalConstants
{
	uint4						mData0					CONSTANT_DEFAULT(uint4(0, 0, 0, 0));
	uint4						mData1					CONSTANT_DEFAULT(uint4(0, 0, 0, 0));
};

#undef GET_COLUMN
#undef RETURN_AS_REFERENCE
#undef CONSTANT_DEFAULT
#undef CONCAT
#undef CONCAT_INNER
#undef GENERATE_PAD_NAME
