#pragma once

#include "Common.h"

class VertexBuffer final
{
public:
	VertexBuffer(void* inData, uint32_t inVertexSize, uint32_t inVertexCount, std::wstring inName);
	~VertexBuffer() {}

	const ComPtr<ID3D12Resource>& GetResource() const { return mResource; }
	uint32_t GetVertexCount() const { return mVertexCount; }
	uint32_t GetVertexSize() const { return mVertexSize; }

private:
	ComPtr<ID3D12Resource> mResource = nullptr;
	uint32_t mVertexCount = 0;
	uint32_t mVertexSize = 0;
};
using VertexBufferRef = std::shared_ptr<VertexBuffer>;

class BLAS final
{
public:
	BLAS(std::wstring inName) { mName = inName; }
	~BLAS() {}

	void Initialize(std::vector<VertexBufferRef>&& inVertexBuffers);
	void Build(ID3D12GraphicsCommandList4* inCommandList);

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return mDest->GetGPUVirtualAddress(); }

private:

	std::vector<VertexBufferRef> mVertexBuffers;

	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> mDescs;
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
	ObjectInstance(BLASRef& inBLAS, const glm::mat4& inTransform, uint32_t inHitGroupIndex, uint32_t inInstanceID)
		: mBLAS(inBLAS)
		, mTransform(inTransform)
		, mInstanceID(inInstanceID)
		, mHitGroupIndex(inHitGroupIndex)
	{
	}

	void Update() { if (mUpdater) mUpdater(this); }
	void SetUpdater(std::function<void(ObjectInstance*)> inUpdater) { mUpdater = inUpdater; }

	BLASRef GetBLAS() { return mBLAS; }
	glm::mat4& GetTransform() { return mTransform; }
	uint32_t GetInstanceID() const { return mInstanceID; }
	uint32_t GetHitGroupIndex() const { return mHitGroupIndex; }

private:
	glm::mat4 mTransform = glm::mat4(1);
	BLASRef mBLAS;
	uint32_t mInstanceID = 0;
	uint32_t mHitGroupIndex = 0;

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

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return mDest->GetGPUVirtualAddress(); }

private:
	void BuildInternal(ID3D12GraphicsCommandList4* inCommandList, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS inFlags);
	void UpdateObjectInstances();

	std::vector<ObjectInstanceRef> mObjectInstances;
	D3D12_RAYTRACING_INSTANCE_DESC* mObjectInstanceDesc = nullptr;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS mInputs = {};

	ComPtr<ID3D12Resource> mScratch = nullptr;
	ComPtr<ID3D12Resource> mDest = nullptr;
	ComPtr<ID3D12Resource> mInstanceDescs = nullptr;

	std::wstring mName;
};
using TLASRef = std::shared_ptr<TLAS>;

class Scene
{
public:
	void Load(const char* inFilename);
	void Unload();

	void Build(ID3D12GraphicsCommandList4* inCommandList);
	void Update(ID3D12GraphicsCommandList4* inCommandList);
	void RebuildBinding(std::function<void()> inCallback);

private:
	TLASRef mTLAS;
};

extern Scene gScene;