#pragma once

#include "Common.h"

enum class GeometryType
{
	Triangles = 0,
	AABB,
	OMM,
	DMM,
	Sphere,
	LSS,
};

struct InstanceInfo
{
	std::string mName;
	std::string mMaterialName;

	struct Texture
	{
		bool empty() const { return mPath.empty(); }
		std::filesystem::path filename() const { return mPath.filename(); }
		std::string string() const { return mPath.string(); }
		std::wstring wstring() const { return mPath.wstring(); }
		
		std::filesystem::path mPath;
		bool mPointSampler = false;
	};

	Texture mAlbedoTexture;
	Texture mNormalTexture;
	Texture mReflectanceTexture;
	Texture mRoughnessTexture;
	Texture mEmissionTexture;
	
	GeometryType mGeometryType = GeometryType::Triangles;
};

class BLAS final
{
public:
	void Initialize(const InstanceInfo& inInstanceInfo, const InstanceData& inInstanceData, D3D12_GPU_VIRTUAL_ADDRESS inVertexBaseAddress, D3D12_GPU_VIRTUAL_ADDRESS inIndexBaseAddress);
	void Build(ID3D12GraphicsCommandList4* inCommandList);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return mDest->GetGPUVirtualAddress(); }

private:
	D3D12_RAYTRACING_GEOMETRY_DESC mDesc{};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS mInputs{};

	NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX mDescEx{};
	NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX mInputsEx{};

	ComPtr<ID3D12Resource> mScratch = nullptr;
	ComPtr<ID3D12Resource> mDest = nullptr;

	bool mBuilt = false;
};
using BLASRef = std::shared_ptr<BLAS>;

using IndexType = uint32_t;
using VertexType = glm::vec3;
using NormalType = glm::vec3;
using UVType = glm::vec2;

struct SceneContent
{
	std::vector<IndexType>						mIndices;
	std::vector<VertexType>						mVertices;
	std::vector<NormalType>						mNormals;
	std::vector<UVType>							mUVs;

	std::vector<InstanceInfo>					mInstanceInfos;
	std::vector<InstanceData>					mInstanceDatas;

	struct EmissiveInstance
	{
		uint									mInstanceIndex;
		uint									mTriangleOffset;
	};
	std::vector<EmissiveInstance>				mEmissiveInstances;
	uint										mEmissiveTriangleCount = 0;

	std::vector<Light>							mLights;

	std::optional<glm::mat4x4>					mCameraTransform;
	std::optional<float>						mFov;

	std::optional<AtmosphereMode>				mAtmosphereMode;
	glm::vec4									mBackgroundColor = glm::vec4(0.0f);
};

class TLAS final
{
public:
	void Initialize(const std::string& inName, const std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& inInstanceDescs);
	void Build(ID3D12GraphicsCommandList4* inCommandList);

	ID3D12Resource* GetResource() const						{ return mDest.Get(); }
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const	{ return mDest->GetGPUVirtualAddress(); }

private:
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS	mInputs {};

	ComPtr<ID3D12Resource>									mScratch;
	ComPtr<ID3D12Resource>									mDest;
	ComPtr<ID3D12Resource>									mInstanceDescs;
};
using TLASRef = std::shared_ptr<TLAS>;

class Scene
{
public:
	void Load(const std::string_view& inFilePath, const glm::mat4x4& inTransform);
	void Unload();

	void Build();
	void Render();

	const SceneContent& GetSceneContent() const					{ return mSceneContent; }

	int GetInstanceCount() const								{ return static_cast<int>(mSceneContent.mInstanceInfos.size()); }
	const InstanceInfo& GetInstanceInfo(int inIndex) const		{ return mSceneContent.mInstanceInfos[inIndex]; }
	const InstanceData& GetInstanceData(int inIndex) const		{ return mSceneContent.mInstanceDatas[inIndex]; }

	int GetLightCount() const									{ return static_cast<int>(mSceneContent.mLights.size()); }
	const Light& GetLight(int inIndex) const					{ return mSceneContent.mLights[inIndex]; }

	int GetPrepareLightsTaskCount() const						{ return static_cast<int>(mBuffers.mTaskBufferCPU.size()); }

	void ImGuiShowTextures()									{ ImGui::Textures(mTextures, "Scene", ImGuiTreeNodeFlags_None); }

private:
	bool LoadDummy(SceneContent& ioContext);
	bool LoadObj(const std::string& inFilename, const glm::mat4x4& inTransform, bool inFlipV, SceneContent& ioContext);
	bool LoadMitsuba(const std::string& inFilename, SceneContent& ioContext);
	bool LoadGLTF(const std::string& inFilename, SceneContent& ioContext);

	void FillDummyMaterial(InstanceInfo& ioInstanceInfo, InstanceData& ioInstanceData);
	
	void InitializeTextures();
	void InitializeBuffers();
	void InitializeAccelerationStructures();
	void InitializeViews();

	struct Primitives
	{
		SceneContent						mCube;
		SceneContent						mRectangle;
		SceneContent						mSphere;
	};
	Primitives								mPrimitives;

	SceneContent							mSceneContent;

	std::vector<BLASRef>					mBlases;
	TLASRef									mTLAS;

	struct Buffers
	{
		ComPtr<ID3D12Resource>				mIndices;
		ComPtr<ID3D12Resource>				mVertices;
		ComPtr<ID3D12Resource>				mNormals;
		ComPtr<ID3D12Resource>				mUVs;

		ComPtr<ID3D12Resource>				mInstanceDatas;
		ComPtr<ID3D12Resource>				mLights;

		// RTXDI
		ComPtr<ID3D12Resource>				mTaskBuffer;
		std::vector<PrepareLightsTask>		mTaskBufferCPU; // CPU copy for debugging
		ComPtr<ID3D12Resource>				mLightDataBuffer;
	};
	Buffers									mBuffers;

	std::vector<Texture>					mTextures;
	uint									mNextSRVIndex = 0;
};

extern Scene gScene;