#include "MLRunner.h"

MLRunner gMLRunner;

void MLRunner::Initialize()
{
    // Data
    {
        for (auto& element : mInput)
            element = 1.618f;

        for (auto& element : mOutput)
            element = 0.0f;
    }

    // Device
    {
        DML_CREATE_DEVICE_FLAGS dml_create_device_flags = DXML_ENABLE_DEBUG_LAYER ? DML_CREATE_DEVICE_FLAG_DEBUG : DML_CREATE_DEVICE_FLAG_NONE;
        gValidate(DMLCreateDevice(gDevice, dml_create_device_flags, IID_PPV_ARGS(&mRuntime.mDevice)));
    }

    // Operator
    {
        DML_BUFFER_TENSOR_DESC buffer_tensor_desc = {};
        buffer_tensor_desc.DataType = DML_TENSOR_DATA_TYPE_FLOAT32;
        buffer_tensor_desc.Flags = DML_TENSOR_FLAG_NONE;
        buffer_tensor_desc.DimensionCount = ARRAYSIZE(kTensorSize);
        buffer_tensor_desc.Sizes = kTensorSize;
        buffer_tensor_desc.Strides = nullptr;
        buffer_tensor_desc.TotalTensorSizeInBytes = kTensorBufferSize;

        DML_TENSOR_DESC tensor_desc{};
        tensor_desc.Type = DML_TENSOR_TYPE_BUFFER;
        tensor_desc.Desc = &buffer_tensor_desc;

        DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC idensity_operator_desc{};
        idensity_operator_desc.InputTensor = &tensor_desc;
        idensity_operator_desc.OutputTensor = &tensor_desc;

        // [NOTE] IDENTITY: f(x) = x, initializer is trivial
        DML_OPERATOR_DESC operator_desc{};
        operator_desc.Type = DML_OPERATOR_ELEMENT_WISE_IDENTITY;
        operator_desc.Desc = &idensity_operator_desc;
        gValidate(mRuntime.mDevice->CreateOperator(&operator_desc, IID_PPV_ARGS(&mRuntime.mOperator)));
        mRuntime.mOperator->SetName(L"MLRunner.mOperator");
    }

    // CompiledOperator
    {
        gValidate(mRuntime.mDevice->CompileOperator(mRuntime.mOperator.Get(), DML_EXECUTION_FLAG_NONE, IID_PPV_ARGS(&mRuntime.mCompiledOperator)));
        mRuntime.mCompiledOperatorBindingProperties = mRuntime.mCompiledOperator->GetBindingProperties();
        mRuntime.mCompiledOperator->SetName(L"MLRunner.mCompiledOperator");
    }

    // OperatorInitializer
    {
        IDMLCompiledOperator* operators[] = { mRuntime.mCompiledOperator.Get() };
        gValidate(mRuntime.mDevice->CreateOperatorInitializer(ARRAYSIZE(operators), operators, IID_PPV_ARGS(&mRuntime.mOperatorInitializer)));
        mRuntime.mOperatorInitializerBindingProperties = mRuntime.mOperatorInitializer->GetBindingProperties();
        mRuntime.mOperatorInitializer->SetName(L"MLRunner.mOperatorInitializer");
    }

    // DescriptorHeap
    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
    {
        mContext[i].mDescriptorHeap.mType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        mContext[i].mDescriptorHeap.mCount = gMax(mRuntime.mCompiledOperatorBindingProperties.RequiredDescriptorCount, mRuntime.mOperatorInitializerBindingProperties.RequiredDescriptorCount);
        mContext[i].mDescriptorHeap.Initialize();
        mContext[i].mDescriptorHeap.mHeap->SetName(L"MLRunner.mDescriptorHeap");
    }

    // OperatorInitializerBindingTable
    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
    {
        DML_BINDING_TABLE_DESC binding_table_desc{};
        binding_table_desc.Dispatchable = mRuntime.mOperatorInitializer.Get();
        binding_table_desc.CPUDescriptorHandle = mContext[i].mDescriptorHeap.mCPUHandleStart;
        binding_table_desc.GPUDescriptorHandle = mContext[i].mDescriptorHeap.mGPUHandleStart;
        binding_table_desc.SizeInDescriptors = mContext[i].mDescriptorHeap.mCount;
        gValidate(mRuntime.mDevice->CreateBindingTable(&binding_table_desc, IID_PPV_ARGS(&mContext[i].mOperatorInitializerBindingTable)));
        mContext[i].mOperatorInitializerBindingTable->SetName(L"MLRunner.mOperatorInitializerBindingTable");
    }

    // CompiledOperatorBindingTable
    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
    {
        DML_BINDING_TABLE_DESC binding_table_desc{};
        binding_table_desc.Dispatchable = mRuntime.mCompiledOperator.Get();
        binding_table_desc.CPUDescriptorHandle = mContext[i].mDescriptorHeap.mCPUHandleStart;
        binding_table_desc.GPUDescriptorHandle = mContext[i].mDescriptorHeap.mGPUHandleStart;
        binding_table_desc.SizeInDescriptors = mContext[i].mDescriptorHeap.mCount;
        gValidate(mRuntime.mDevice->CreateBindingTable(&binding_table_desc, IID_PPV_ARGS(&mContext[i].mCompiledOperatorBindingTable)));
        mContext[i].mCompiledOperatorBindingTable->SetName(L"MLRunner.mCompiledOperatorBindingTable");
    }

    // TemporaryResource
    {
        D3D12_HEAP_PROPERTIES heap_properties = gGetDefaultHeapProperties();
        D3D12_RESOURCE_DESC resource_desc = gGetUAVResourceDesc(gMax(gMax(mRuntime.mOperatorInitializerBindingProperties.TemporaryResourceSize, mRuntime.mCompiledOperatorBindingProperties.TemporaryResourceSize), 1ull /* dummy */));
        gValidate(gDevice->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mRuntime.mTemporaryResource)));
        mRuntime.mTemporaryResource->SetName(L"MLRunner.mTemporaryResource");
    }

    // PersistentResource
    {
        D3D12_HEAP_PROPERTIES heap_properties = gGetDefaultHeapProperties();
        D3D12_RESOURCE_DESC resource_desc = gGetUAVResourceDesc(gMax(mRuntime.mOperatorInitializerBindingProperties.PersistentResourceSize, 1ull));
        gValidate(gDevice->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mRuntime.mPersistentResource)));
        mRuntime.mPersistentResource->SetName(L"MLRunner.mPersistentResource");
    }

    // InputResource
    {
        D3D12_HEAP_PROPERTIES heap_properties = gGetDefaultHeapProperties();
        D3D12_RESOURCE_DESC resource_desc = gGetUAVResourceDesc(kTensorBufferSize);
        gValidate(gDevice->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mRuntime.mInputResource)));
        mRuntime.mInputResource->SetName(L"MLRunner.mInputResource");
    }

    // OutputResource
    {
        D3D12_HEAP_PROPERTIES heap_properties = gGetDefaultHeapProperties();
        D3D12_RESOURCE_DESC resource_desc = gGetUAVResourceDesc(kTensorBufferSize);
        gValidate(gDevice->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&mRuntime.mOutputResource)));
        mRuntime.mOutputResource->SetName(L"MLRunner.mOutputResource");
    }

    // UploadResource
    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
    {
        D3D12_HEAP_PROPERTIES heap_properties = gGetUploadHeapProperties();
        D3D12_RESOURCE_DESC resource_desc = gGetBufferResourceDesc(kTensorBufferSize);
        if (resource_desc.Width != 0)
        {
            gValidate(gDevice->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&mContext[i].mUploadResource)));
            mContext[i].mUploadResource->SetName(L"MLRunner.mUploadResource");
            mContext[i].mUploadResource->Map(0, nullptr, &mContext[i].mUploadResourcePointer);
        }
    }

    // ReadbackResource
    for (int i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
    {
        D3D12_HEAP_PROPERTIES heap_properties = gGetReadbackHeapProperties();
        D3D12_RESOURCE_DESC resource_desc = gGetBufferResourceDesc(kTensorBufferSize);
        if (resource_desc.Width != 0)
        {
            gValidate(gDevice->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mContext[i].mReadbackResource)));
            mContext[i].mReadbackResource->SetName(L"MLRunner.mReadbackResource");
            mContext[i].mReadbackResource->Map(0, nullptr, &mContext[i].mReadbackResourcePointer);
        }
    }

    // CommandRecorder
    {
        gValidate(mRuntime.mDevice->CreateCommandRecorder(IID_PPV_ARGS(&mRuntime.mCommandRecorder)));
        mRuntime.mCommandRecorder->SetName(L"MLRunner.mCommandRecorder");
    }
}

void MLRunner::Render()
{
    if (!mEnabled)
        return;

    uint context_index = gFrameIndex % NUM_FRAMES_IN_FLIGHT;

    ID3D12DescriptorHeap* heaps[] = { mContext[context_index].mDescriptorHeap.mHeap.Get() };
    gCommandList->SetDescriptorHeaps(ARRAYSIZE(heaps), heaps);

    // OperatorInitilizer
    if (!mOperatorInitialized)
    {
        // Bind Temporary
        {
            DML_BUFFER_BINDING buffer_binding{ mRuntime.mTemporaryResource.Get(), 0, mRuntime.mOperatorInitializerBindingProperties.TemporaryResourceSize };
            DML_BINDING_DESC binding_desc{ mRuntime.mOperatorInitializerBindingProperties.TemporaryResourceSize == 0 ? DML_BINDING_TYPE_NONE : DML_BINDING_TYPE_BUFFER, &buffer_binding };
            mContext[context_index].mOperatorInitializerBindingTable->BindTemporaryResource(&binding_desc);
        }

        // Bind Persistent
        {
            DML_BUFFER_BINDING buffer_binding{ mRuntime.mPersistentResource.Get(), 0, mRuntime.mOperatorInitializerBindingProperties.PersistentResourceSize };
            DML_BINDING_DESC binding_desc{ mRuntime.mOperatorInitializerBindingProperties.PersistentResourceSize == 0 ? DML_BINDING_TYPE_NONE : DML_BINDING_TYPE_BUFFER, &buffer_binding };
            mContext[context_index].mOperatorInitializerBindingTable->BindOutputs(1, &binding_desc);
        }

        // Dispatch
        {
            mRuntime.mCommandRecorder->RecordDispatch(gCommandList, mRuntime.mOperatorInitializer.Get(), mContext[context_index].mOperatorInitializerBindingTable.Get());
        }

        mOperatorInitialized = true;
    }

    // CompiledOperator
    {
        // Upload
        {
            memcpy(mContext[context_index].mUploadResourcePointer, mInput.data(), kTensorBufferSize);

            BarrierScope input_scope(gCommandList, mRuntime.mInputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
            gCommandList->CopyResource(mRuntime.mInputResource.Get(), mContext[context_index].mUploadResource.Get());
        }

        // Bind Temporary
        {
            DML_BUFFER_BINDING buffer_binding { mRuntime.mTemporaryResource.Get(), 0, mRuntime.mCompiledOperatorBindingProperties.TemporaryResourceSize };
            DML_BINDING_DESC binding_desc { mRuntime.mCompiledOperatorBindingProperties.TemporaryResourceSize == 0 ? DML_BINDING_TYPE_NONE : DML_BINDING_TYPE_BUFFER, &buffer_binding };
            mContext[context_index].mCompiledOperatorBindingTable->BindTemporaryResource(&binding_desc);
        }

        // Bind Persistent
        {
            DML_BUFFER_BINDING buffer_binding { mRuntime.mPersistentResource.Get(), 0, mRuntime.mCompiledOperatorBindingProperties.PersistentResourceSize };
            DML_BINDING_DESC binding_desc { mRuntime.mCompiledOperatorBindingProperties.PersistentResourceSize == 0 ? DML_BINDING_TYPE_NONE : DML_BINDING_TYPE_BUFFER, &buffer_binding };
            mContext[context_index].mCompiledOperatorBindingTable->BindPersistentResource(&binding_desc);
        }

        // Bind Input
        {
            DML_BUFFER_BINDING buffer_binding{ mRuntime.mInputResource.Get(), 0, kTensorBufferSize };
            DML_BINDING_DESC binding_desc { DML_BINDING_TYPE_BUFFER, &buffer_binding };
            mContext[context_index].mCompiledOperatorBindingTable->BindInputs(1, &binding_desc);
        }

        // Bind Output
        {
            DML_BUFFER_BINDING buffer_binding{ mRuntime.mOutputResource.Get(), 0, kTensorBufferSize };
            DML_BINDING_DESC binding_desc{ DML_BINDING_TYPE_BUFFER, &buffer_binding };
            mContext[context_index].mCompiledOperatorBindingTable->BindOutputs(1, &binding_desc);
        }

        // Dispatch
        {
            mRuntime.mCommandRecorder->RecordDispatch(gCommandList, mRuntime.mCompiledOperator.Get(), mContext[context_index].mCompiledOperatorBindingTable.Get());
        }

        // Readback
        {
            memcpy(mOutput.data(), mContext[context_index].mReadbackResourcePointer, kTensorBufferSize);

            BarrierScope output_scope(gCommandList, mRuntime.mOutputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            gCommandList->CopyResource(mContext[context_index].mReadbackResource.Get(), mRuntime.mOutputResource.Get());
        }
    }
}

void MLRunner::ImGui()
{
    ImGui::Checkbox("Enabled", &mEnabled);

    ImGui::PushItemWidth(100);

    for (int i = 0; i < kTensorElementCount; i++)
    {
        ImGui::InputFloat(std::format("Input {}", i).c_str(), &mInput[i], 0, 0, "%.8f", ImGuiInputTextFlags_None);

        ImGui::SameLine(100 + 100);

        ImGui::InputFloat(std::format("Output {}", i).c_str(), &mOutput[i], 0, 0, "%.8f", ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::PopItemWidth();
}