#pragma once

#include "Common.h"

class Primitive final
{
public:
	Primitive(glm::uint32 inVertexOffset, glm::uint32 inVertexCount, glm::uint32 inIndexOffset, glm::uint32 inIndexCount) :
		mVertexOffset(inVertexOffset),
		mVertexCount(inVertexCount),
		mIndexOffset(inIndexOffset),
		mIndexCount(inIndexCount) {}

	glm::uint32 GetVertexOffset() const { return mVertexOffset; }
	glm::uint32 GetVertexCount() const { return mVertexCount; }

	glm::uint32 GetIndexOffset() const { return mIndexOffset; }
	glm::uint32 GetIndexCount() const { return mIndexCount; }

private:
	glm::uint32 mVertexOffset = 0;
	glm::uint32 mVertexCount = 0;

	glm::uint32 mIndexOffset = 0;
	glm::uint32 mIndexCount = 0;
};
using PrimitiveRef = std::shared_ptr<Primitive>;

class BLAS final
{
public:
	BLAS(PrimitiveRef inPrimitive, std::string_view inName) : mPrimitive(inPrimitive), mName(inName) {}

	void Initialize(D3D12_GPU_VIRTUAL_ADDRESS inVertexBaseAddress, D3D12_GPU_VIRTUAL_ADDRESS inIndexBaseAddress);
	void Build(ID3D12GraphicsCommandList4* inCommandList);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return mDest->GetGPUVirtualAddress(); }
	const std::string& GetName() { return mName; }

private:
	PrimitiveRef mPrimitive;

	D3D12_RAYTRACING_GEOMETRY_DESC mDesc{};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS mInputs{};

	ComPtr<ID3D12Resource> mScratch = nullptr;
	ComPtr<ID3D12Resource> mDest = nullptr;

	std::string mName;

	bool mBuilt = false;
};
using BLASRef = std::shared_ptr<BLAS>;

class ObjectInstance
{
public:
	ObjectInstance(const BLASRef& inBLAS, const glm::mat4& inTransform, glm::uint32 inHitGroupIndex)
		: mBLAS(inBLAS)
		, mTransform(inTransform)
		, mHitGroupIndex(inHitGroupIndex)
	{
	}

	const BLASRef GetBLAS() const { return mBLAS; }
	const glm::mat4& GetTransform() const { return mTransform; }
	glm::mat4& Transform() { return mTransform; }
	glm::uint32 GetHitGroupIndex() const { return mHitGroupIndex; }

	InstanceData mInstanceData;

	std::string mName;
	std::string mMaterialName;

private:
	glm::mat4 mTransform = glm::mat4(1);
	BLASRef mBLAS;
	glm::uint32 mHitGroupIndex = 0;
};
using ObjectInstanceRef = std::shared_ptr<ObjectInstance>;

class TLAS final
{
public:
	explicit TLAS(const std::string& inName) : mName(inName) {}

	void Initialize(std::vector<ObjectInstanceRef>&& inObjectInstances);

	void Update(ID3D12GraphicsCommandList4* inCommandList);
	void Build(ID3D12GraphicsCommandList4* inCommandList);

	int GetInstanceCount() const							{ return static_cast<int>(mObjectInstances.size()); }
	const ObjectInstance& GetInstance(int inIndex)			{ return *mObjectInstances[inIndex]; }

	ID3D12Resource* GetResource() const						{ return mDest.Get(); }
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const	{ return mDest->GetGPUVirtualAddress(); }
	ID3D12Resource* GetInstanceBuffer()	const				{ return mInstanceBuffer.Get(); }

private:
	void BuildInternal(ID3D12GraphicsCommandList4* inCommandList, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS inFlags);
	void UpdateObjectInstances();

	std::vector<ObjectInstanceRef> mObjectInstances {};

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS mInputs {};

	ComPtr<ID3D12Resource> mScratch = nullptr;
	ComPtr<ID3D12Resource> mDest = nullptr;
	ComPtr<ID3D12Resource> mInstanceDescs = nullptr;
	D3D12_RAYTRACING_INSTANCE_DESC* mInstanceDescsPointer = nullptr;

	ComPtr<ID3D12Resource> mInstanceBuffer = nullptr;
	InstanceData* mInstanceBufferPointer = nullptr;

	std::string mName;
};
using TLASRef = std::shared_ptr<TLAS>;

class Scene
{
public:
	using IndexType = glm::uint32;
	using VertexType = glm::vec3;
	using NormalType = glm::vec3;
	using UVType = glm::vec2;

	void Load(const char* inFilename, const glm::mat4x4& inTransform);
	void Unload();

	void Build(ID3D12GraphicsCommandList4* inCommandList);
	void Update(ID3D12GraphicsCommandList4* inCommandList);
	void RebuildBinding(std::function<void()> inCallback);
	void RebuildShader();

	int GetInstanceCount() const					{ return mTLAS->GetInstanceCount(); }
	const ObjectInstance& GetInstance(int inIndex)	{ return mTLAS->GetInstance(inIndex); }

	ID3D12Resource* GetOutputResource()				{ return mOutputResource.Get(); }

	ID3D12DescriptorHeap* GetDXRDescriptorHeap()	{ return mDXRDescriptorHeap.Get(); }

private:	
	struct ObjectCollection
	{		
		std::vector<ObjectInstanceRef>		mObjectInstances;
		std::vector<IndexType>				mIndices;
		std::vector<VertexType>				mVertices;
		std::vector<NormalType>				mNormals;
		std::vector<UVType>					mUVs;
	};
	bool LoadDummy(ObjectCollection& ioContext);
	bool LoadObj(const std::string& inFilename, const glm::mat4x4& inTransform, ObjectCollection& ioContext);
	bool LoadMitsuba(const std::string& inFilename, ObjectCollection& ioContext);

	void FillDummyMaterial(InstanceData& ioInstanceData);
	
	void InitializeAS(ObjectCollection& inContext);

	void CreateShaderResource();
	void CleanupShaderResource();

	struct PrimitiveObjectCollection
	{
		ObjectCollection					mCube;
		ObjectCollection					mRectangle;
		ObjectCollection					mSphere;
	};
	PrimitiveObjectCollection				mPrimitiveObjectCollection;

	TLASRef									mTLAS;

	ComPtr<ID3D12Resource>					mIndexBuffer = nullptr;
	ComPtr<ID3D12Resource>					mVertexBuffer = nullptr;	
	ComPtr<ID3D12Resource>					mNormalBuffer = nullptr;
	ComPtr<ID3D12Resource>					mUVBuffer = nullptr;

	ComPtr<ID3D12Resource>					mOutputResource = nullptr;

	ComPtr<ID3D12DescriptorHeap>			mDXRDescriptorHeap = nullptr;
};

extern Scene gScene;