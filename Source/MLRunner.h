#pragma once

#include "Common.h"

#define DXML_ENABLE_DEBUG_LAYER (1)

struct MLRunner
{
    constexpr static UINT kTensorSize[4]        = { 1, 2, 3, 4 };
    constexpr static UINT kTensorElementCount   = kTensorSize[0] * kTensorSize[1] * kTensorSize[2] * kTensorSize[3];
    constexpr static UINT kTensorBufferSize     = (kTensorElementCount * sizeof(float) + 3) & ~3ull; // Round up to 4 bytes

    std::array<float, kTensorElementCount>      mInput;
    std::array<float, kTensorElementCount>      mOutput;

    bool                                        mEnabled = false;
    bool                                        mOperatorInitialized = false;

	struct Runtime : RuntimeBase<Runtime>
	{
        ComPtr<IDMLDevice>                      mDevice;

        // [NOTE] IDMLOperator: shader source
        ComPtr<IDMLOperator>                    mOperator;
        // [NOTE] IDMLCompiledOperator: shader binary
        ComPtr<IDMLCompiledOperator>            mCompiledOperator;
        DML_BINDING_PROPERTIES                  mCompiledOperatorBindingProperties {};

        // [NOTE] IDMLOperatorInitializer: initialization shader for operator
        ComPtr<IDMLOperatorInitializer>         mOperatorInitializer;
        DML_BINDING_PROPERTIES                  mOperatorInitializerBindingProperties {};

        ComPtr<ID3D12Resource>                  mTemporaryResource;     // Scratch
        ComPtr<ID3D12Resource>                  mPersistentResource;    // Operator
        ComPtr<ID3D12Resource>                  mInputResource;         // Input -> Operator
        ComPtr<ID3D12Resource>                  mOutputResource;        // Output <- Operator

        ComPtr<IDMLCommandRecorder>             mCommandRecorder;
	};
	Runtime mRuntime;

    struct FrameContext
    {
        DescriptorHeap<uint>		            mDescriptorHeap;

        // [NOTE] IDMLBindingTable: basically PSO (Shader) + Descriptor Root Table
        ComPtr<IDMLBindingTable>                mCompiledOperatorBindingTable;
        ComPtr<IDMLBindingTable>                mOperatorInitializerBindingTable;

        ComPtr<ID3D12Resource>                  mUploadResource;        // Upload -> Input
        void*                                   mUploadResourcePointer = nullptr;

        ComPtr<ID3D12Resource>                  mReadbackResource;      // Readback <- Output
        void*                                   mReadbackResourcePointer = nullptr;
    };
    FrameContext mContext[NUM_FRAMES_IN_FLIGHT];

    void									    Initialize();
	void									    Finalize() { mRuntime.Reset(); }
    void                                        Render();
    void                                        ImGui();
};
extern MLRunner								    gMLRunner;