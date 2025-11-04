#pragma once

#include "Shared.h"
#include "HLSL.h"
#include "NanoVDB.h"

#if USE_HALF
using half_ = half;
using half3_ = half3;
#else
using half = float;
using half3 = float3;
#endif // USE_HALF

half_ ashalf(float inValue) { return (half_)inValue; }
half3_ ashalf(float3 inValue) { return (half3_)inValue; }

struct PixelContext
{
	uint3			mPixelIndex;
	uint3			mPixelTotal;
					
	float			mDepth;
	uint			mOutputDepth : 1;
};

struct PathContext
{
	float3			mThroughput;					// [0, 1]		Accumulated throughput, [PBRT3] call it beta https://github.com/mmp/pbrt-v3/blob/master/src/integrators/path.cpp#L68
	float3			mEmission;						// [0, +inf]	Accumulated emission
	float			mEtaScale;
					
	float3			mLightEmission;					// [0, +inf]	Emission from light sample
					
	float			mPrevBSDFSamplePDF;
	uint			mPrevDiracDeltaDistribution : 1;
					
	uint			mRandomState;
	uint			mRandomStateReSTIR;

	uint			mRecursionDepth : 8;
	uint			mMediumInstanceID : 16;
};

// Context information about a point on surface
struct SurfaceContext
{	
	void			LoadSurface()
	{
		// Only support 32bit index for simplicity
		// see https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl for reference
		uint base_index					= mPrimitiveIndex * kIndexCountPerTriangle + mInstanceData.mIndexOffset;
		uint3 indices					= uint3(Indices[base_index], Indices[base_index + 1], Indices[base_index + 2]) + mInstanceData.mVertexOffset;

		mVertexPositions[0]				= Vertices[indices[0]];
		mVertexPositions[1]				= Vertices[indices[1]];
		mVertexPositions[2]				= Vertices[indices[2]];
		mVertexPositionOS				= mVertexPositions[0] * mBarycentrics.x + mVertexPositions[1] * mBarycentrics.y + mVertexPositions[2] * mBarycentrics.z;

		mVertexNormals[0]				= Normals[indices[0]];
		mVertexNormals[1]				= Normals[indices[1]];
		mVertexNormals[2]				= Normals[indices[2]];
		float3 normal					= normalize(mVertexNormals[0] * mBarycentrics.x + mVertexNormals[1] * mBarycentrics.y + mVertexNormals[2] * mBarycentrics.z);
		mVertexNormalOS					= normal;
		mVertexNormalWS					= normalize(mul((float3x3) mInstanceData.mInverseTranspose, normal)); // Allow non-uniform scale

		mUV								= 0;
		if (mInstanceData.mFlags.mUV)
		{
			mVertexUVs[0]				= UVs[indices[0]];
			mVertexUVs[1]				= UVs[indices[1]];
			mVertexUVs[2]				= UVs[indices[2]];
			mUV							= mVertexUVs[0] * mBarycentrics.x + mVertexUVs[1] * mBarycentrics.y + mVertexUVs[2] * mBarycentrics.z;
		}
	}
	
	BSDF			BSDF()
	{
		if (mConstants.mDebugInstanceIndex == mInstanceID)
		{
			switch (mConstants.mDebugInstanceMode)
			{
			case DebugInstanceMode::Barycentrics:				return BSDF::Diffuse;
			case DebugInstanceMode::Reflection:					return BSDF::Conductor;
			default: break;
			}
		}

		return mInstanceData.mBSDF;
	}
	uint			TwoSided()					{ return mInstanceData.mFlags.mTwoSided; }
	float			Opacity()					{ return mInstanceData.mOpacity; }
	uint			LightIndex()				{ return mInstanceData.mLightIndex; }
	float			RoughnessAlpha()
	{
		if (BSDF() == BSDF::pbrMetallicRoughness) { return RoughnessAlphaGLTF(); }
		
		return mInstanceData.mRoughnessAlpha;
	}
	float3			Albedo()
	{
		if (BSDF() == BSDF::pbrMetallicRoughness) { return AlbedoGLTF(); }
		
#if USE_TEXTURE
		uint texture_index = mInstanceData.mAlbedoTexture.mTextureIndex;
		uint sampler_index = mInstanceData.mAlbedoTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * mInstanceData.mAlbedo;
		}
#endif // USE_TEXTURE
		return mInstanceData.mAlbedo; 
	}
	float3			SpecularReflectance()
	{
		if (BSDF() == BSDF::pbrMetallicRoughness) { return SpecularReflectanceGLTF(); }
		
#if USE_TEXTURE
		uint texture_index = mInstanceData.mReflectanceTexture.mTextureIndex;
		uint sampler_index = mInstanceData.mReflectanceTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * mInstanceData.mReflectance;
		}
#endif // USE_TEXTURE
		return mInstanceData.mReflectance;
	}
	float3			SpecularTransmittance()		{ return mInstanceData.mSpecularTransmittance; }
	float3			Eta()						{ return mInstanceData.mEta; }
	float3			K()							{ return mInstanceData.mK; }
	float			Metallic()
	{
		if (BSDF() == BSDF::pbrMetallicRoughness) { return MetallicGLTF(); }
		
		return 0;
	}

	float3			Emission()					
	{
#if USE_TEXTURE
		uint texture_index = mInstanceData.mEmissionTexture.mTextureIndex;
		uint sampler_index = mInstanceData.mEmissionTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * mInstanceData.mEmission;
		}
#endif // USE_TEXTURE
		return mInstanceData.mEmission; 
	}

	bool			HasMedium()					{ return mInstanceData.mMedium != 0; }

	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metal-brdf-and-dielectric-brdf
	float3			AlbedoGLTF()						
	{
		float3 base_color = mInstanceData.mAlbedo;
		
#if USE_TEXTURE
		uint texture_index = mInstanceData.mAlbedoTexture.mTextureIndex;
		uint sampler_index = mInstanceData.mAlbedoTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			base_color = texture.SampleLevel(sampler, mUV, 0).rgb * mInstanceData.mAlbedo;
		}
#endif // USE_TEXTURE

		float metallic = MetallicGLTF();
		return lerp(base_color, 0.0, metallic);
	}
	float3			SpecularReflectanceGLTF()
	{
		float3 base_color = mInstanceData.mAlbedo;
		
#if USE_TEXTURE
		uint texture_index = mInstanceData.mAlbedoTexture.mTextureIndex;
		uint sampler_index = mInstanceData.mAlbedoTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			base_color = texture.SampleLevel(sampler, mUV, 0).rgb * mInstanceData.mAlbedo;
		}
#endif // USE_TEXTURE

		float metallic = MetallicGLTF();
		return lerp(DielectricReflectanceGLTF(), base_color, metallic);
	}
	float			DielectricReflectanceGLTF() { return 0.04; }
	float			MetallicGLTF()
	{
#if USE_TEXTURE
		uint texture_index = mInstanceData.mReflectanceTexture.mTextureIndex;
		uint sampler_index = mInstanceData.mReflectanceTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).b * mInstanceData.mReflectance.x;
		}
#endif // USE_TEXTURE

		return mInstanceData.mReflectance.x;
	}
	float			RoughnessAlphaGLTF()
	{
#if USE_TEXTURE
		uint texture_index = mInstanceData.mReflectanceTexture.mTextureIndex;
		uint sampler_index = mInstanceData.mReflectanceTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			float roughness = texture.SampleLevel(sampler, mUV, 0).g;
			return (roughness * roughness) * mInstanceData.mRoughnessAlpha;
		}
#endif // USE_TEXTURE

		return max(0.01, mInstanceData.mRoughnessAlpha); // Extra clamp
	}

	bool			DiracDeltaDistribution()
	{
		switch (BSDF())
		{
		case BSDF::Dielectric:					return true;
		case BSDF::ThinDielectric:				return true;
		case BSDF::Conductor:					return true;
		default:								return false;
		}
	}

	float3			Barycentrics()				{ return mBarycentrics; }
	float2			UV()						{ return mUV; }

	InstanceData	mInstanceData;

	uint			mInstanceID;
	uint			mPrimitiveIndex;
	float3			mBarycentrics;

	// Vertex Attributes
	float3			mVertexPositions[3];
	float3			mVertexNormals[3];
	float2			mVertexUVs[3];

	// Interpolated Vertex Attributes
	float3			mVertexPositionOS;
	float3			mVertexNormalOS;
	float3			mVertexNormalWS;
	float2			mUV;
};

struct Ray
{
	static Ray Generate(RayDesc inRayDesc, float inTCurrent)
	{
		Ray ray;
		ray.mOrigin								= inRayDesc.Origin;
		ray.mDirection							= inRayDesc.Direction;
		ray.mTCurrent							= inTCurrent;
		return ray;
	}
	
	float3			Target()					{ return mOrigin + mDirection * mTCurrent; }
	
	float3			mOrigin;
	float3			mDirection;
	float			mTCurrent;
};

struct HitContext : SurfaceContext
{
	template<RAY_FLAG RayFlags>
	static HitContext Generate(RayDesc inRayDesc, RayQuery<RayFlags> inRayQuery)
	{
		// System value intrinsics https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#system-value-intrinsics
		
		float2 bary2							= inRayQuery.CommittedTriangleBarycentrics();
		
		HitContext hit_context					= (HitContext)0;
		hit_context.mInstanceData				= InstanceDatas[inRayQuery.CommittedInstanceID()];
		hit_context.mInstanceID					= inRayQuery.CommittedInstanceID();
		hit_context.mPrimitiveIndex				= inRayQuery.CommittedPrimitiveIndex();
		hit_context.mBarycentrics				= float3(1.0 - bary2.x - bary2.y, bary2.x, bary2.y);
		hit_context.mRayWS.mOrigin				= inRayDesc.Origin;
		hit_context.mRayWS.mDirection			= inRayDesc.Direction;
		hit_context.mRayWS.mTCurrent			= inRayQuery.CommittedRayT();

		hit_context.LoadSurface();

#ifdef NVAPI_LSS
		if (NvRtCommittedIsLss(inRayQuery))
		{
			// Transform from object space to handle non-uniform scale properly
			// See also getGeometryFromHit in RTXCR
			// [NOTE] Somehow the object space coordinates here might flip sign. Avoid get position from it
			float2x4 lss_data					= NvRtCommittedLssObjectPositionsAndRadii(inRayQuery);
			float3 lss_center_OS				= lerp(lss_data[0].xyz, lss_data[1].xyz, bary2.x); // bary2.x = NvRtCommittedLssHitParameter(inRayQuery)
			float3 lss_position_OS				= inRayQuery.CommittedObjectRayOrigin() + inRayQuery.CommittedRayT() * inRayQuery.CommittedObjectRayDirection();
			float3 lss_normal_OS				= lss_position_OS - lss_center_OS;
			hit_context.mVertexNormalWS			= normalize(mul((float3x3)hit_context.mInstanceData.mInverseTranspose, lss_normal_OS)); // Allow non-uniform scale

			// Clear data those are not available
			hit_context.mUV						= 0;
			hit_context.mVertexPositionOS		= 0;
			hit_context.mVertexNormalOS			= 0;
		}
#endif // NVAPI_LSS
		
		return hit_context;
	}
	
	float3			PositionWS()				{ return mRayWS.Target(); }
	float3			DirectionWS()				{ return mRayWS.mDirection; }
	float3			ViewWS()					{ return -mRayWS.mDirection; }
	float3			NormalWS()
	{
		float3 view								= ViewWS();
		float3 normal							= mVertexNormalWS;

		// Handle TwoSided
		if (dot(normal, view) < 0 && TwoSided())
			normal								= -normal;

		return normal;
	}
	float			NdotV()						{ return dot(NormalWS(), ViewWS()); }

	Ray				mRayWS;
};

// Context information for BSDF evalution at the hit point
struct BSDFContext
{
	// [NOTE] All vectors should be in the same space. Tangent space seems to be the best fit, where N = (0,0,1), then e.g NdotL = L.z
	//     [Mitsuba3] Tangent space

	enum class Mode
	{
		BSDF,
		Light,
	};
	Mode			mMode;

	static BSDFContext Generate(Mode inMode, float3 inLight, float inEtaIT, bool inLobe0Selected, HitContext inHitContext)
	{
		BSDFContext bsdf_context;

		bsdf_context.mMode						= inMode;

		bsdf_context.mL							= inLight;
		bsdf_context.mN							= inHitContext.NormalWS();
		bsdf_context.mV							= inHitContext.ViewWS();
		bsdf_context.mH							= normalize(bsdf_context.mV + bsdf_context.mL * inEtaIT);	// See roughdielectric::eval

		if (dot(bsdf_context.mN, bsdf_context.mH) < 0)
			bsdf_context.mH						= -bsdf_context.mH; // Put H on the same side as N
	
		bsdf_context.mNdotV						= dot(bsdf_context.mN, bsdf_context.mV);
		bsdf_context.mNdotL						= dot(bsdf_context.mN, bsdf_context.mL);
		bsdf_context.mNdotH						= dot(bsdf_context.mN, bsdf_context.mH);
		bsdf_context.mHdotV						= dot(bsdf_context.mH, bsdf_context.mV);
		bsdf_context.mHdotL						= dot(bsdf_context.mH, bsdf_context.mL);

		bsdf_context.mLobe0Selected				= inLobe0Selected;

		return bsdf_context;
	}

	static BSDFContext Generate(Mode inMode, float3 inLight, HitContext inHitContext)
	{
		float dummy_eta_it						= 1.0;
		bool dummy_lobe0_selected				= false;

		BSDFContext bsdf_context				= Generate(inMode, inLight, dummy_eta_it, dummy_lobe0_selected, inHitContext);

		// Patch
		bsdf_context.mLobe0Selected				= bsdf_context.mNdotV * bsdf_context.mNdotL > 0;
		// [NOTE] eta_it can not be determined until BSDF is evaluated. SetEta is used then.

		return bsdf_context;
	}

	void			SetEta(float inEtaIT)
	{
		mH										= normalize(mV + mL * inEtaIT);	// See roughdielectric::eval
		if (dot(mN, mH) < 0)
			mH									= -mH; // Put H on the same side as N

		mNdotH									= dot(mN, mH);
		mHdotV									= dot(mH, mV);
		mHdotL									= dot(mH, mL);
	}

	void			FlipNormal()
	{
		mN										= -mN;
		mH										= -mH;

		mNdotV									= -mNdotV;
		mNdotL									= -mNdotL;

		mHdotV									= -mHdotV;
		mHdotL									= -mHdotL;
	}

	float3			mL;
	float3			mN;
	float3			mV;
	float3			mH;

	float			mNdotH;
	float			mNdotV;
	float			mNdotL;
	float			mHdotV;
	float			mHdotL;

	bool			mLobe0Selected;				// [TODO] More than 2 lobes? Use lobe index? Still coupled with implementaion detail

	float			mLPDF;						// For light sample
};

struct MediumContext
{
	void ApplyNanoVDB(inout PathContext ioPathContext)
	{
		if (mInstanceData.mMediumNanoVBD.mBufferIndex == (uint)ViewDescriptorIndex::Invalid)
			return;

		float density							= SampleNanoVDB(PositionOS(), mInstanceData.mMediumNanoVBD);
		mSigmaT									*= density * mConstants.mDensityBoost;
		mSigmaT									= min(mSigmaT, mMajorantSigmaT); // using SigmaT in xml as majorant, clamp with it as max is ignored

		// DebugValue(DebugMode::Manual, ioPathContext.mRecursionDepth, normalized_coords);
		// DebugValue(DebugMode::Manual, ioPathContext.mRecursionDepth, ijk);
		// DebugValue(DebugMode::Manual, ioPathContext.mRecursionDepth, density);
		// DebugValue(DebugMode::Manual, ioPathContext.mRecursionDepth, mMajorantSigmaT);
		// DebugValue(DebugMode::Manual, ioPathContext.mRecursionDepth, mInstanceData.mMediumSigmaT);
	}

	void ApplySequence(inout PathContext ioPathContext)
	{
		if (mConstants.mSequenceEnabled == 0)
			return;

		float ratio								= mConstants.mSequenceFrameRatio;

		if (mInstanceData.mMediumNanoVBD.mBufferIndex != (uint)ViewDescriptorIndex::Invalid)
		{
			mSigmaT								*= (ratio - 0.75) / (1.0 - 0.75);
			return;
		}

		Texture3D<float> ErosionNoise3D			= ResourceDescriptorHeap[(int)ViewDescriptorIndex::ErosionNoise3DSRV];
		float3 offset							= float3(0, -mConstants.mSequenceFrameRatio * 0.5, 0);
		float noise_value						= ErosionNoise3D.SampleLevel(BilinearWrapSampler, (PositionWS() + offset) * 1.0, 0);
		noise_value								= saturate(pow(noise_value * 1.5, 4.0));

		float y_gradient						= pow(saturate(PositionWS().y + 0.1), 0.2);
		noise_value								= lerp(noise_value, 0.0f, lerp(y_gradient, 0.0, pow(ratio, 16.0)));
		
		mSigmaT									*= noise_value;
	}

	void ApplySequenceOnce()
	{
		if (mConstants.mSequenceEnabled == 0)
			return;

		float ratio								= mConstants.mSequenceFrameRatio;

		if (mInstanceData.mMediumNanoVBD.mBufferIndex != (uint)ViewDescriptorIndex::Invalid)
		{
			mMajorantSigmaT						*= (ratio - 0.75) / (1.0 - 0.75);
			return;
		}
	}

	template<RAY_FLAG RayFlags>
	static MediumContext Generate(RayDesc inRayDesc, RayQuery<RayFlags> inRayQuery)
	{
		MediumContext medium_context;
		medium_context.mInstanceData			= InstanceDatas[inRayQuery.CommittedInstanceID()];
		medium_context.mInstanceID				= inRayQuery.CommittedInstanceID();

		medium_context.mRayWS.mOrigin			= inRayDesc.Origin;
		medium_context.mRayWS.mDirection		= inRayDesc.Direction;
		medium_context.mRayWS.mTCurrent			= inRayQuery.CommittedRayT();

		medium_context.mRayOS.mOrigin			= inRayQuery.CommittedObjectRayOrigin();
		medium_context.mRayOS.mDirection		= inRayQuery.CommittedObjectRayDirection();
		medium_context.mRayOS.mTCurrent			= inRayQuery.CommittedRayT();
		
		medium_context.mMajorantSigmaT			= medium_context.mInstanceData.mMediumSigmaT;
		if (medium_context.mInstanceData.mMediumNanoVBD.mBufferIndex != (uint)ViewDescriptorIndex::Invalid)
			medium_context.mMajorantSigmaT		*= mConstants.mDensityBoost;
		medium_context.mSigmaT					= 0;

		medium_context.ApplySequenceOnce();

		return medium_context;
	}

	void			ScatterAt(float inT, inout PathContext ioPathContext)
	{
		mSigmaT									= mInstanceData.mMediumSigmaT;

		mRayWS.mTCurrent						= inT;
		mRayOS.mTCurrent						= inT;

		ApplyNanoVDB(ioPathContext);
		ApplySequence(ioPathContext);
	}

	float3			PositionWS()				{ return mRayWS.Target(); }
	float3			DirectionWS()				{ return mRayWS.mDirection; }
	float3			ViewWS()					{ return -mRayWS.mDirection; }

	float3			PositionOS()				{ return mRayOS.Target(); }
	float3			DirectionOS()				{ return mRayOS.mDirection; }
	float3			ViewOS()					{ return -mRayOS.mDirection; }

	float3			Albedo()					{ return mInstanceData.mMediumAlbedo; }
	float3			SigmaT()					{ return mSigmaT; }
	float			Phase()						{ return mInstanceData.mMediumPhase; }

	float3			Transmittance(float inDistance) { return exp(-SigmaT() * inDistance); }

	InstanceData	mInstanceData;

	uint			mInstanceID;

	Ray				mRayWS;
	Ray				mRayOS;

	float3			mMajorantSigmaT;
	float3			mSigmaT;
};
