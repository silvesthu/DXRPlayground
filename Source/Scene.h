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
	BLAS(PrimitiveRef inPrimitive, std::wstring inName) :
		mPrimitive(inPrimitive), mName(inName) {}
	~BLAS() {}

	void Initialize(D3D12_GPU_VIRTUAL_ADDRESS inVertexBaseAddress, D3D12_GPU_VIRTUAL_ADDRESS inIndexBaseAddress);
	void Build(ID3D12GraphicsCommandList4* inCommandList);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return mDest->GetGPUVirtualAddress(); }

private:
	PrimitiveRef mPrimitive;

	D3D12_RAYTRACING_GEOMETRY_DESC mDesc;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS mInputs = {};

	ComPtr<ID3D12Resource> mScratch = nullptr;
	ComPtr<ID3D12Resource> mDest = nullptr;

	std::wstring mName;

	bool mBuilt = false;
};
using BLASRef = std::shared_ptr<BLAS>;

class ObjectInstance
{
public:
	ObjectInstance(BLASRef& inBLAS, const glm::mat4& inTransform, glm::uint32 inHitGroupIndex)
		: mBLAS(inBLAS)
		, mTransform(inTransform)
		, mHitGroupIndex(inHitGroupIndex)
	{
	}

	void Update() { if (mUpdater) mUpdater(this); }
	void SetUpdater(std::function<void(ObjectInstance*)> inUpdater) { mUpdater = inUpdater; }

	BLASRef GetBLAS() { return mBLAS; }
	glm::mat4& Transform() { return mTransform; }
	glm::uint32 GetHitGroupIndex() const { return mHitGroupIndex; }
	ShaderType::InstanceData& Data() { return mInstanceData; }

private:
	glm::mat4 mTransform = glm::mat4(1);
	BLASRef mBLAS;
	glm::uint32 mHitGroupIndex = 0;
	ShaderType::InstanceData mInstanceData;

	std::function<void(ObjectInstance*)> mUpdater;
};
using ObjectInstanceRef = std::shared_ptr<ObjectInstance>;

class TLAS final
{
public:
	TLAS(std::wstring inName) { mName = inName; }
	~TLAS() {}

	void Initialize(std::vector<ObjectInstanceRef>&& inObjectInstances);

	void Update(ID3D12GraphicsCommandList4* inCommandList);
	void Build(ID3D12GraphicsCommandList4* inCommandList);

	glm::uint32 GetInstanceCount() const { return (glm::uint32)mObjectInstances.size(); }

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const	{ return mDest->GetGPUVirtualAddress(); }
	ID3D12Resource* GetInstanceBuffer()	const				{ return mInstanceBuffer.Get(); }

private:
	void BuildInternal(ID3D12GraphicsCommandList4* inCommandList, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS inFlags);
	void UpdateObjectInstances();

	std::vector<ObjectInstanceRef> mObjectInstances;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS mInputs = {};

	ComPtr<ID3D12Resource> mScratch = nullptr;
	ComPtr<ID3D12Resource> mDest = nullptr;
	ComPtr<ID3D12Resource> mInstanceDescs = nullptr;
	D3D12_RAYTRACING_INSTANCE_DESC* mInstanceDescsPointer = nullptr;

	ComPtr<ID3D12Resource> mInstanceBuffer = nullptr;
	ShaderType::InstanceData* mInstanceBufferPointer = nullptr;

	std::wstring mName;
};
using TLASRef = std::shared_ptr<TLAS>;

class Scene
{
public:
	using IndexType = glm::uint32; // glm::uint16;
	using VertexType = glm::vec3;
	using NormalType = glm::vec3;

	void Load(const char* inFilename, const glm::mat4x4& inTransform);
	void Unload();

	void Build(ID3D12GraphicsCommandList4* inCommandList);
	void Update(ID3D12GraphicsCommandList4* inCommandList);
	void RebuildBinding(std::function<void()> inCallback);
	void RebuildShader();

	glm::uint32 GetInstanceCount() const { return mTLAS->GetInstanceCount(); }

	ID3D12Resource* GetOutputResource() { return mOutputResource.Get(); }

	ID3D12DescriptorHeap* GetDXRDescriptorHeap() { return mDXRDescriptorHeap.Get(); }

private:
	void CreateShaderResource();
	void CleanupShaderResource();

	TLASRef							mTLAS;

	ComPtr<ID3D12Resource>			mIndexBuffer = nullptr;
	ComPtr<ID3D12Resource>			mVertexBuffer = nullptr;	
	ComPtr<ID3D12Resource>			mNormalBuffer = nullptr;

	ComPtr<ID3D12Resource>			mOutputResource = nullptr;

	ComPtr<ID3D12DescriptorHeap>	mDXRDescriptorHeap = nullptr;
};

extern Scene gScene;