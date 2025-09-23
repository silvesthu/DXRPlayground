#pragma once

struct PixelContext
{
	uint3			mPixelIndex;
	uint3			mPixelTotal;
					
	bool			mOutputDepth;
	float			mDepth;
};

struct PathContext
{
	float3			mThroughput;					// [0, 1]		Accumulated throughput, [PBRT3] call it beta https://github.com/mmp/pbrt-v3/blob/master/src/integrators/path.cpp#L68
	float3			mEmission;						// [0, +inf]	Accumulated emission
	float			mEtaScale;
					
	float3			mLightEmission;					// [0, +inf]	Emission from light sample
					
	float			mPrevBSDFSamplePDF;
	bool			mPrevDiracDeltaDistribution;
					
	uint			mRandomState;
	uint			mRecursionDepth;

	uint			mRandomStateReSTIR;
};

// Context information about a point on surface
struct SurfaceContext
{
	InstanceData	InstanceData()				{ return InstanceDatas[mInstanceID]; }
	
	void			LoadSurface()
	{
		// Only support 32bit index for simplicity
		// see https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingSimpleLighting/Raytracing.hlsl for reference
		uint base_index					= mPrimitiveIndex * kIndexCountPerTriangle + InstanceData().mIndexOffset;
		uint3 indices					= uint3(Indices[base_index], Indices[base_index + 1], Indices[base_index + 2]) + InstanceData().mVertexOffset;

		mVertexPositions[0]				= Vertices[indices[0]];
		mVertexPositions[1]				= Vertices[indices[1]];
		mVertexPositions[2]				= Vertices[indices[2]];
		mVertexPositionOS				= mVertexPositions[0] * mBarycentrics.x + mVertexPositions[1] * mBarycentrics.y + mVertexPositions[2] * mBarycentrics.z;

		mVertexNormals[0]				= Normals[indices[0]];
		mVertexNormals[1]				= Normals[indices[1]];
		mVertexNormals[2]				= Normals[indices[2]];
		float3 normal					= normalize(mVertexNormals[0] * mBarycentrics.x + mVertexNormals[1] * mBarycentrics.y + mVertexNormals[2] * mBarycentrics.z);
		mVertexNormalOS					= normal;
		mVertexNormalWS					= normalize(mul((float3x3) InstanceData().mInverseTranspose, normal)); // Allow non-uniform scale

		mVertexUVs[0]					= UVs[indices[0]];
		mVertexUVs[1]					= UVs[indices[1]];
		mVertexUVs[2]					= UVs[indices[2]];
		mUV								= mVertexUVs[0] * mBarycentrics.x + mVertexUVs[1] * mBarycentrics.y + mVertexUVs[2] * mBarycentrics.z;
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

		return InstanceData().mBSDF;
	}
	uint			TwoSided()					{ return InstanceData().mTwoSided; }
	float			Opacity()					{ return InstanceData().mOpacity; }
	uint			LightIndex()				{ return InstanceData().mLightIndex; }
	float			RoughnessAlpha()
	{
		if (BSDF() == BSDF::pbrMetallicRoughness) { return RoughnessAlphaGLTF(); }
		
		return InstanceData().mRoughnessAlpha;
	}
	float3			Albedo()
	{
		if (BSDF() == BSDF::pbrMetallicRoughness) { return AlbedoGLTF(); }
		
		uint texture_index = InstanceData().mAlbedoTexture.mTextureIndex;
		uint sampler_index = InstanceData().mAlbedoTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * InstanceData().mAlbedo;
		}
		return InstanceData().mAlbedo; 
	}
	float3			SpecularReflectance()
	{
		if (BSDF() == BSDF::pbrMetallicRoughness) { return SpecularReflectanceGLTF(); }
		
		uint texture_index = InstanceData().mReflectanceTexture.mTextureIndex;
		uint sampler_index = InstanceData().mReflectanceTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * InstanceData().mReflectance;
		}
		return InstanceData().mReflectance;
	}
	float3			SpecularTransmittance()		{ return InstanceData().mSpecularTransmittance; }
	float3			Eta()						{ return InstanceData().mEta; }
	float3			K()							{ return InstanceData().mK; }
	float			Metallic()
	{
		if (BSDF() == BSDF::pbrMetallicRoughness) { return MetallicGLTF(); }
		
		return 0;
	}
	float3			Emission()					
	{
		uint texture_index = InstanceData().mEmissionTexture.mTextureIndex;
		uint sampler_index = InstanceData().mEmissionTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).rgb * InstanceData().mEmission;
		}
		return InstanceData().mEmission; 
	}

	// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metal-brdf-and-dielectric-brdf
	float3			AlbedoGLTF()						
	{
		float3 base_color = InstanceData().mAlbedo;
		
		uint texture_index = InstanceData().mAlbedoTexture.mTextureIndex;
		uint sampler_index = InstanceData().mAlbedoTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			base_color = texture.SampleLevel(sampler, mUV, 0).rgb * InstanceData().mAlbedo;
		}

		float metallic = MetallicGLTF();
		return lerp(base_color, 0.0, metallic);
	}
	float3			SpecularReflectanceGLTF()
	{
		float3 base_color = InstanceData().mAlbedo;
		
		uint texture_index = InstanceData().mAlbedoTexture.mTextureIndex;
		uint sampler_index = InstanceData().mAlbedoTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			base_color = texture.SampleLevel(sampler, mUV, 0).rgb * InstanceData().mAlbedo;
		}

		float metallic = MetallicGLTF();
		return lerp(DielectricReflectanceGLTF(), base_color, metallic);
	}
	float			DielectricReflectanceGLTF() { return 0.04; }
	float			MetallicGLTF()
	{
		uint texture_index = InstanceData().mReflectanceTexture.mTextureIndex;
		uint sampler_index = InstanceData().mReflectanceTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			return texture.SampleLevel(sampler, mUV, 0).b * InstanceData().mReflectance.x;
		}
		return InstanceData().mReflectance.x;
	}
	float			RoughnessAlphaGLTF()
	{
		uint texture_index = InstanceData().mReflectanceTexture.mTextureIndex;
		uint sampler_index = InstanceData().mReflectanceTexture.mSamplerIndex;
		if (texture_index != (uint)ViewDescriptorIndex::Invalid)
		{
			Texture2D<float4> texture = ResourceDescriptorHeap[texture_index];
			SamplerState sampler = SamplerDescriptorHeap[sampler_index];
			float roughness = texture.SampleLevel(sampler, mUV, 0).g;
			return (roughness * roughness) * InstanceData().mRoughnessAlpha;
		}
		return max(0.01, InstanceData().mRoughnessAlpha); // Extra clamp
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
	static Ray Generate(RayDesc inRayDesc)
	{
		Ray ray;
		ray.mOrigin								= inRayDesc.Origin;
		ray.mDirection							= inRayDesc.Direction;
		ray.mTCurrent							= inf();
		return ray;
	}
	
	float3			Target()					{ return mOrigin + mDirection * mTCurrent; }
	
	float3			mOrigin;
	float3			mDirection;
	float			mTCurrent;
};

struct HitContext : SurfaceContext
{
	template <RAY_FLAG RayFlags>
	static HitContext Generate(RayDesc inRayDesc, RayQuery<RayFlags> inRayQuery)
	{
		// System value intrinsics https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#system-value-intrinsics
		
		float2 bary2							= inRayQuery.CommittedTriangleBarycentrics();
		
		HitContext hit_context					= (HitContext)0;
		
		hit_context.mInstanceID					= inRayQuery.CommittedInstanceID();
		hit_context.mPrimitiveIndex				= inRayQuery.CommittedPrimitiveIndex();
		hit_context.mBarycentrics				= float3(1.0 - bary2.x - bary2.y, bary2.x, bary2.y);
		hit_context.mRayWS.mOrigin				= inRayDesc.Origin;
		hit_context.mRayWS.mDirection			= inRayDesc.Direction;
		hit_context.mRayWS.mTCurrent			= inRayQuery.CommittedRayT();

		hit_context.LoadSurface();
		
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
