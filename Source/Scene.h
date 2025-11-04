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
	TriangleAsLSS,
};

struct InstanceInfo
{
	struct Material
	{
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

		struct NanoVDB
		{
			std::filesystem::path mPath;
		};

		Texture mAlbedoTexture;
		Texture mNormalTexture;
		Texture mReflectanceTexture;
		Texture mRoughnessTexture;
		Texture mEmissionTexture;

		NanoVDB mNanoVDB;
	};

	std::string mName;

	Material mMaterial;

	GeometryType mGeometryType = GeometryType::Triangles;

	float3 mDecomposedScale = float3(1.0f);
};

class BLAS final
{
public:
	struct Initializer
	{
		const InstanceInfo& mInstanceInfo;
		const InstanceData& mInstanceData;

		D3D12_GPU_VIRTUAL_ADDRESS mVerticesBaseAddress;
		D3D12_GPU_VIRTUAL_ADDRESS mIndicesBaseAddress;

		D3D12_GPU_VIRTUAL_ADDRESS mLSSIndicesBaseAddress;
		D3D12_GPU_VIRTUAL_ADDRESS mLSSRadiiBaseAddress;
	};

	void Initialize(const Initializer& inInitializer);
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

using RadiusType = float;

struct SceneContent
{
	std::vector<IndexType>						mIndices;
	std::vector<VertexType>						mVertices;
	std::vector<NormalType>						mNormals;
	std::vector<UVType>							mUVs;

	std::vector<InstanceInfo>					mInstanceInfos;
	std::vector<InstanceData>					mInstanceDatas;

	struct InstanceAnimation
	{
		std::vector<glm::vec3>					mTranslation;
		std::vector<glm::vec4>					mRotation;
		std::vector<glm::vec3>					mScale;
	};
	struct Camera
	{
		bool									mHasAnimation = false;
		InstanceAnimation						mAnimation;
	};
	Camera										mCamera;

	struct EmissiveInstance
	{
		uint									mInstanceIndex;
		uint									mTriangleOffset;
	};
	std::vector<EmissiveInstance>				mEmissiveInstances;
	uint										mEmissiveTriangleCount = 0;

	std::vector<Light>							mLights;

	std::set<BSDF>								mBSDFs;

	std::optional<glm::mat4x4>					mCameraTransform;
	std::optional<float>						mFov;

	std::optional<AtmosphereMode>				mAtmosphereMode;

	bool										mLSSAllowed = false;
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

struct ScenePreset
{
#define SCENE_PRESET_MEMBER(type, name, default_value) MEMBER(ScenePreset, type, name, default_value)

	SCENE_PRESET_MEMBER(std::string_view, 		Name, 					"");
	SCENE_PRESET_MEMBER(std::string_view, 		Path, 					"");
	SCENE_PRESET_MEMBER(glm::vec4, 				CameraPosition, 		glm::vec4(0, 1, 0, 1));
	SCENE_PRESET_MEMBER(glm::vec4, 				CameraDirection, 		glm::vec4(0, 0, -1, 0));
	SCENE_PRESET_MEMBER(LightSourceMode,		LightSource, 			LightSourceMode::Emitter);
	SCENE_PRESET_MEMBER(float, 					EmissionBoost, 			1.0f); // As no auto exposure yet
	SCENE_PRESET_MEMBER(float, 					DensityBoost, 			1.0f);
	SCENE_PRESET_MEMBER(float, 					HorizontalFovDegree, 	90);
	SCENE_PRESET_MEMBER(glm::mat4x4, 			Transform, 				glm::mat4x4(1));
	SCENE_PRESET_MEMBER(float, 					SunAzimuth, 			0);
	SCENE_PRESET_MEMBER(float, 					SunZenith, 				glm::pi<float>() / 4.0f);
	SCENE_PRESET_MEMBER(AtmosphereMode,			Atmosphere,				AtmosphereMode::ConstantColor);
	SCENE_PRESET_MEMBER(glm::vec4,				ConstantColor,			glm::vec4(0, 0, 0, 0));
	SCENE_PRESET_MEMBER(bool,					TriangleAsLSSAllowed,	false);
	SCENE_PRESET_MEMBER(std::string_view, 		CameraAnimationPath,	"");
};

class Scene
{
public:
	void Load(const ScenePreset& inPreset);
	void Unload();

	void Build();
	void Render();

	const SceneContent& GetSceneContent() const					{ return mSceneContent; }

	int GetInstanceCount() const								{ return static_cast<int>(mSceneContent.mInstanceInfos.size()); }
	const InstanceInfo& GetInstanceInfo(int inIndex) const		{ return mSceneContent.mInstanceInfos[inIndex]; }
	const InstanceData& GetInstanceData(int inIndex) const		{ return mSceneContent.mInstanceDatas[inIndex]; }

	int GetLightCount() const									{ return static_cast<int>(mSceneContent.mLights.size()); }
	const Light& GetLight(int inIndex) const					{ return mSceneContent.mLights[inIndex]; }

	int GetPrepareLightsTaskCount() const						{ return static_cast<int>(mRuntime.mTaskBufferCPU.size()); }

	void ImGuiShowTextures()									{ ImGui::Textures(mTextures, "Scene", ImGuiTreeNodeFlags_None); }

private:
	bool LoadDummy(SceneContent& ioContext);
	bool LoadObj(const std::string& inFilename, const glm::mat4x4& inTransform, bool inFlipV, SceneContent& ioContext);
	bool LoadMitsuba(const std::string& inFilename, SceneContent& ioContext);
	bool LoadGLTF(const std::string& inFilename, SceneContent& ioContext);

	void FillDummyMaterial(InstanceInfo& ioInstanceInfo, InstanceData& ioInstanceData);
	
	void GenerateLSSFromTriangle();
	void InitializeTextures();
	void InitializeBuffers();
	void InitializeRuntime();
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

	struct Runtime
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

		// LSS
		ComPtr<ID3D12Resource>				mLSSIndices;
		ComPtr<ID3D12Resource>				mLSSRadii;
	};
	Runtime									mRuntime;

	std::vector<Texture>					mTextures;
 	std::vector<Buffer>						mBuffers;

	struct BufferVisualization
	{
		uint								mInstanceIndex = 0;
		uint								mBufferIndex = 0;
		uint								mTexutureIndex = 0;
	};
	std::vector<BufferVisualization>		mBufferVisualizations;

	uint									mNextViewDescriptorIndex = 0;
};

extern Scene gScene;