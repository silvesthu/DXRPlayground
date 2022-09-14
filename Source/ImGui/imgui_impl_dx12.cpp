// dear imgui: Renderer for DirectX12
// This needs to be used along with a Platform Binding (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'D3D12_GPU_DESCRIPTOR_HANDLE' as ImTextureID. Read the FAQ about ImTextureID in imgui.cpp.
// Issues:
//  [ ] 64-bit only for now! (Because sizeof(ImTextureId) == sizeof(void*)). See github.com/ocornut/imgui/pull/301

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you are new to dear imgui, read examples/README.txt and read the documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2019-04-30: DirectX12: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2019-03-29: Misc: Various minor tidying up.
//  2018-12-03: Misc: Added #pragma comment statement to automatically link with d3dcompiler.lib when using D3DCompile().
//  2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About Window.
//  2018-06-12: DirectX12: Moved the ID3D12GraphicsCommandList* parameter from NewFrame() to RenderDrawData().
//  2018-06-08: Misc: Extracted imgui_impl_dx12.cpp/.h away from the old combined DX12+Win32 example.
//  2018-06-08: DirectX12: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle (to ease support for future multi-viewport).
//  2018-02-22: Merged into master with all Win32 code synchronized to other examples.

#include "../Thirdparty/imgui/imgui.h"
#include "imgui_impl_dx12.h"

// DirectX
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#ifdef _MSC_VER
#pragma comment(lib, "d3d12")
#pragma comment(lib, "dxgi")		// CreateDeviceD3D()
#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.
#pragma comment(lib, "dxguid")		// For DXGI_DEBUG_ALL
#endif

// DirectX data
static ID3D12Device*                g_D3DDevice = NULL;
static ID3D10Blob*                  g_VertexShaderBlob = NULL;
static ID3D10Blob*                  g_PixelShaderBlob = NULL;
static ID3D12RootSignature*         g_RootSignature = NULL;
static ID3D12PipelineState*         g_PipelineState = NULL;
static DXGI_FORMAT                  g_RTVFormat = DXGI_FORMAT_UNKNOWN;
static ID3D12Resource*              g_FontTextureResource = NULL;
static D3D12_CPU_DESCRIPTOR_HANDLE  g_FontSrvCpuDescHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE  g_FontSrvGpuDescHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_Null2DSrvCpuDescHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE  g_Null2DSrvGpuDescHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_Null3DSrvCpuDescHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE  g_Null3DSrvGpuDescHandle = {};

struct Texture
{
    D3D12_RESOURCE_DESC             mDesc = {};
    float                           mMin = 0;
    float                           mMax = 1;
    int                             mDepthIndex = 0;
    bool                            mAlpha = false;
    D3D12_CPU_DESCRIPTOR_HANDLE     mCPUHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE     mGPUHandle;
};

static const int                    kDescriptorSize = 4096;
static Texture                      g_Textures[kDescriptorSize];

// DirectX data - Customization
static ID3D12DescriptorHeap*        g_DescriptorHeap = NULL;
static int                          g_DescriptorHeapNextIndex = 0;
static UINT                         g_DescriptorHeapIncrementSize = 0;

struct FrameResources
{
    ID3D12Resource*     IndexBuffer;
    ID3D12Resource*     VertexBuffer;
    int                 IndexBufferSize;
    int                 VertexBufferSize;
};
static FrameResources*  g_FrameResources = NULL;
static UINT             g_NumFramesInFlight = 0;
static UINT             g_FrameIndex = UINT_MAX;

struct CONSTANT_BUFFER
{
    float   mvp[4][4];
};

static void ImGui_ImplDX12_SetupRenderState(ImDrawData* draw_data, ID3D12GraphicsCommandList* ctx, FrameResources* fr)
{
    // Setup orthographic projection matrix into our constant buffer
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
    CONSTANT_BUFFER constant_buffer;
    {
        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        float mvp[4][4] =
        {
            { 2.0f/(R-L),   0.0f,           0.0f,       0.0f },
            { 0.0f,         2.0f/(T-B),     0.0f,       0.0f },
            { 0.0f,         0.0f,           0.5f,       0.0f },
            { (R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f },
        };
        memcpy(&constant_buffer.mvp, mvp, sizeof(mvp));
    }

    // Setup viewport
    D3D12_VIEWPORT vp;
    memset(&vp, 0, sizeof(D3D12_VIEWPORT));
    vp.Width = draw_data->DisplaySize.x;
    vp.Height = draw_data->DisplaySize.y;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = vp.TopLeftY = 0.0f;
    ctx->RSSetViewports(1, &vp);

    // Bind shader and vertex buffers
    unsigned int stride = sizeof(ImDrawVert);
    unsigned int offset = 0;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    memset(&vbv, 0, sizeof(D3D12_VERTEX_BUFFER_VIEW));
    vbv.BufferLocation = fr->VertexBuffer->GetGPUVirtualAddress() + offset;
    vbv.SizeInBytes = fr->VertexBufferSize * stride;
    vbv.StrideInBytes = stride;
    ctx->IASetVertexBuffers(0, 1, &vbv);
    D3D12_INDEX_BUFFER_VIEW ibv;
    memset(&ibv, 0, sizeof(D3D12_INDEX_BUFFER_VIEW));
    ibv.BufferLocation = fr->IndexBuffer->GetGPUVirtualAddress();
    ibv.SizeInBytes = fr->IndexBufferSize * sizeof(ImDrawIdx);
    ibv.Format = sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    ctx->IASetIndexBuffer(&ibv);
    ctx->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->SetPipelineState(g_PipelineState);
    ctx->SetGraphicsRootSignature(g_RootSignature);
    ctx->SetGraphicsRoot32BitConstants(0, sizeof(CONSTANT_BUFFER) / 4, &constant_buffer, 0);

    // Setup blend factor
    const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->OMSetBlendFactor(blend_factor);
}

// Render function
// (this used to be set in io.RenderDrawListsFn and called by ImGui::Render(), but you can now call this directly from your main loop)
void ImGui_ImplDX12_RenderDrawData(ImDrawData* draw_data, ID3D12GraphicsCommandList* ctx)
{
    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    ctx->SetDescriptorHeaps(1, &g_DescriptorHeap);

    // FIXME: I'm assuming that this only gets called once per frame!
    // If not, we can't just re-allocate the IB or VB, we'll have to do a proper allocator.
    g_FrameIndex = g_FrameIndex + 1;
    FrameResources* fr = &g_FrameResources[g_FrameIndex % g_NumFramesInFlight];

    // Create and grow vertex/index buffers if needed
    if (fr->VertexBuffer == NULL || fr->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (fr->VertexBuffer != NULL) { fr->VertexBuffer->Release(); fr->VertexBuffer = NULL; }
        fr->VertexBufferSize = draw_data->TotalVtxCount + 5000;
        D3D12_HEAP_PROPERTIES props;
        memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
        props.Type = D3D12_HEAP_TYPE_UPLOAD;
        props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        D3D12_RESOURCE_DESC desc;
        memset(&desc, 0, sizeof(D3D12_RESOURCE_DESC));
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = fr->VertexBufferSize * sizeof(ImDrawVert);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&fr->VertexBuffer)) < 0)
            return;
        fr->VertexBuffer->SetName(L"ImGui.VertexBuffer");
    }
    if (fr->IndexBuffer == NULL || fr->IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (fr->IndexBuffer != NULL) { fr->IndexBuffer->Release(); fr->IndexBuffer = NULL; }
        fr->IndexBufferSize = draw_data->TotalIdxCount + 10000;
        D3D12_HEAP_PROPERTIES props;
        memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
        props.Type = D3D12_HEAP_TYPE_UPLOAD;
        props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        D3D12_RESOURCE_DESC desc;
        memset(&desc, 0, sizeof(D3D12_RESOURCE_DESC));
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = fr->IndexBufferSize * sizeof(ImDrawIdx);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&fr->IndexBuffer)) < 0)
            return;
        fr->IndexBuffer->SetName(L"ImGui.IndexBuffer");
    }

    // Upload vertex/index data into a single contiguous GPU buffer
    void* vtx_resource, *idx_resource;
    D3D12_RANGE range;
    memset(&range, 0, sizeof(D3D12_RANGE));
    if (fr->VertexBuffer->Map(0, &range, &vtx_resource) != S_OK)
        return;
    if (fr->IndexBuffer->Map(0, &range, &idx_resource) != S_OK)
        return;
    ImDrawVert* vtx_dst = (ImDrawVert*)vtx_resource;
    ImDrawIdx* idx_dst = (ImDrawIdx*)idx_resource;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }
    fr->VertexBuffer->Unmap(0, &range);
    fr->IndexBuffer->Unmap(0, &range);

    // Setup desired DX state
    ImGui_ImplDX12_SetupRenderState(draw_data, ctx, fr);

    // Render command lists
    int vtx_offset = 0;
    int idx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != NULL)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplDX12_SetupRenderState(draw_data, ctx, fr);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                // Apply Scissor, Bind texture, Draw
                const D3D12_RECT r = { (LONG)(pcmd->ClipRect.x - clip_off.x), (LONG)(pcmd->ClipRect.y - clip_off.y), (LONG)(pcmd->ClipRect.z - clip_off.x), (LONG)(pcmd->ClipRect.w - clip_off.y) };

                D3D12_GPU_DESCRIPTOR_HANDLE start_handle = g_DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
                D3D12_GPU_DESCRIPTOR_HANDLE handle = *(D3D12_GPU_DESCRIPTOR_HANDLE*)&pcmd->TextureId;
                UINT64 index = (handle.ptr - start_handle.ptr) / g_DescriptorHeapIncrementSize;
                Texture& texture = g_Textures[index];

                if (texture.mDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
                {
                    ctx->SetGraphicsRootDescriptorTable(1, handle);
                    ctx->SetGraphicsRootDescriptorTable(2, g_Null3DSrvGpuDescHandle);
                }
                else
                {
                    ctx->SetGraphicsRootDescriptorTable(1, g_Null2DSrvGpuDescHandle);
                    ctx->SetGraphicsRootDescriptorTable(2, handle);
                }

				float constant_buffer[] = { 0, 1, 0, 0 };
				if (g_FontSrvGpuDescHandle.ptr != (*(D3D12_GPU_DESCRIPTOR_HANDLE*)&pcmd->TextureId).ptr)
				{
                    // Only apply on draw of texture
					constant_buffer[0] = texture.mMin;
					constant_buffer[1] = texture.mMax;
					constant_buffer[2] = (texture.mDepthIndex + 0.5f) / texture.mDesc.DepthOrArraySize;
					constant_buffer[3] = texture.mAlpha ? 1.0f : 0.0f;
				};
				ctx->SetGraphicsRoot32BitConstants(3, 4, &constant_buffer, 0);

                ctx->RSSetScissorRects(1, &r);
                ctx->DrawIndexedInstanced(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
            }
            idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
    }
}

static void ImGui_ImplDX12_CreateFontsTexture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Upload texture to graphics system
    {
        D3D12_HEAP_PROPERTIES props;
        memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
        props.Type = D3D12_HEAP_TYPE_DEFAULT;
        props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ID3D12Resource* pTexture = NULL;
        g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&pTexture));
        pTexture->SetName(L"ImGui.FontTexture");

        UINT uploadPitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
        UINT uploadSize = height * uploadPitch;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = uploadSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        props.Type = D3D12_HEAP_TYPE_UPLOAD;
        props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        ID3D12Resource* uploadBuffer = NULL;
        HRESULT hr = g_D3DDevice->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&uploadBuffer));
        IM_ASSERT(SUCCEEDED(hr));
        uploadBuffer->SetName(L"ImGui.FontTexture");

        void* mapped = NULL;
        D3D12_RANGE range = { 0, uploadSize };
        hr = uploadBuffer->Map(0, &range, &mapped);
        IM_ASSERT(SUCCEEDED(hr));
        for (int y = 0; y < height; y++)
            memcpy((void*) ((uintptr_t) mapped + y * uploadPitch), pixels + y * width * 4, width * 4);
        uploadBuffer->Unmap(0, &range);

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = uploadBuffer;
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srcLocation.PlacedFootprint.Footprint.Width = width;
        srcLocation.PlacedFootprint.Footprint.Height = height;
        srcLocation.PlacedFootprint.Footprint.Depth = 1;
        srcLocation.PlacedFootprint.Footprint.RowPitch = uploadPitch;

        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource = pTexture;
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex = 0;

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = pTexture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        ID3D12Fence* fence = NULL;
        hr = g_D3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        IM_ASSERT(SUCCEEDED(hr));

        HANDLE event = CreateEvent(0, 0, 0, 0);
        IM_ASSERT(event != NULL);

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.NodeMask = 1;

        ID3D12CommandQueue* cmdQueue = NULL;
        hr = g_D3DDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
        IM_ASSERT(SUCCEEDED(hr));

        ID3D12CommandAllocator* cmdAlloc = NULL;
        hr = g_D3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
        IM_ASSERT(SUCCEEDED(hr));

        ID3D12GraphicsCommandList* cmdList = NULL;
        hr = g_D3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, NULL, IID_PPV_ARGS(&cmdList));
        IM_ASSERT(SUCCEEDED(hr));

        cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, NULL);
        cmdList->ResourceBarrier(1, &barrier);

        hr = cmdList->Close();
        IM_ASSERT(SUCCEEDED(hr));

        cmdQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*) &cmdList);
        hr = cmdQueue->Signal(fence, 1);
        IM_ASSERT(SUCCEEDED(hr));

        fence->SetEventOnCompletion(1, event);
        WaitForSingleObject(event, INFINITE);

        cmdList->Release();
        cmdAlloc->Release();
        cmdQueue->Release();
        CloseHandle(event);
        fence->Release();
        uploadBuffer->Release();

        // Create texture view
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        g_D3DDevice->CreateShaderResourceView(pTexture, &srvDesc, g_FontSrvCpuDescHandle);
        if (g_FontTextureResource != NULL)
            g_FontTextureResource->Release();
        g_FontTextureResource = pTexture;

        g_D3DDevice->CreateShaderResourceView(nullptr, &srvDesc, g_Null2DSrvCpuDescHandle);

        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = desc.MipLevels;
        srvDesc.Texture3D.MostDetailedMip = 0;
        g_D3DDevice->CreateShaderResourceView(nullptr, &srvDesc, g_Null3DSrvCpuDescHandle);
    }

    // Store our identifier
    static_assert(sizeof(ImTextureID) >= sizeof(g_FontSrvGpuDescHandle.ptr), "Can't pack descriptor handle into TexID, 32-bit not supported yet.");
    io.Fonts->TexID = (ImTextureID)g_FontSrvGpuDescHandle.ptr;
}

bool    ImGui_ImplDX12_CreateDeviceObjects()
{
    if (!g_D3DDevice)
        return false;
    if (g_PipelineState)
        ImGui_ImplDX12_InvalidateDeviceObjects();

    // Create the root signature
    {
        D3D12_DESCRIPTOR_RANGE descRange = {};
        descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRange.NumDescriptors = 1;
        descRange.BaseShaderRegister = 0;
        descRange.RegisterSpace = 0;
        descRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER param[4] = {};

        param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param[0].Constants.ShaderRegister = 0;
        param[0].Constants.RegisterSpace = 0;
        param[0].Constants.Num32BitValues = sizeof(CONSTANT_BUFFER) / 4;
        param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param[1].DescriptorTable.NumDescriptorRanges = 1;
        param[1].DescriptorTable.pDescriptorRanges = &descRange;
        param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_DESCRIPTOR_RANGE descRange_space1 = {};
        descRange_space1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descRange_space1.NumDescriptors = 1;
        descRange_space1.BaseShaderRegister = 0;
        descRange_space1.RegisterSpace = 1;
        descRange_space1.OffsetInDescriptorsFromTableStart = 0;

		param[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param[2].DescriptorTable.NumDescriptorRanges = 1;
		param[2].DescriptorTable.pDescriptorRanges = &descRange_space1;
		param[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		param[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		param[3].Constants.ShaderRegister = 1;
		param[3].Constants.RegisterSpace = 0;
		param[3].Constants.Num32BitValues = 4;
		param[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC staticSampler = {};
        staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.MipLODBias = 0.f;
        staticSampler.MaxAnisotropy = 0;
        staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        staticSampler.MinLOD = 0.f;
        staticSampler.MaxLOD = 0.f;
        staticSampler.ShaderRegister = 0;
        staticSampler.RegisterSpace = 0;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(param);
        desc.pParameters = param;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers = &staticSampler;
        desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        ID3DBlob* blob = NULL;
        if (D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, NULL) != S_OK)
            return false;

        g_D3DDevice->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_RootSignature));
        blob->Release();
    }

    // By using D3DCompile() from <d3dcompiler.h> / d3dcompiler.lib, we introduce a dependency to a given version of d3dcompiler_XX.dll (see D3DCOMPILER_DLL_A)
    // If you would like to use this DX12 sample code but remove this dependency you can:
    //  1) compile once, save the compiled shader blobs into a file or source code and pass them to CreateVertexShader()/CreatePixelShader() [preferred solution]
    //  2) use code to detect any version of the DLL and grab a pointer to D3DCompile from the DLL.
    // See https://github.com/ocornut/imgui/pull/638 for sources and details.

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    memset(&psoDesc, 0, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.NodeMask = 1;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.pRootSignature = g_RootSignature;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = g_RTVFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    // Create the vertex shader
    {
        static const char* vertexShader =
            "cbuffer vertexBuffer : register(b0) \
            {\
              float4x4 ProjectionMatrix; \
            };\
            cbuffer constantBuffer : register(b1) \
            {\
              float    TextureMin; \
              float    TextureMax; \
              float    TextureDepthSlice; \
              float    TextureAlpha; \
            };\
            struct VS_INPUT\
            {\
              float2 pos : POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            struct PS_INPUT\
            {\
              float4 pos : SV_POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            PS_INPUT main(VS_INPUT input)\
            {\
              PS_INPUT output;\
              output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
              output.col = input.col;\
              output.uv  = input.uv;\
              return output;\
            }";

        D3DCompile(vertexShader, strlen(vertexShader), NULL, NULL, NULL, "main", "vs_5_1", D3DCOMPILE_ALL_RESOURCES_BOUND, 0, &g_VertexShaderBlob, NULL);
        if (g_VertexShaderBlob == NULL) // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
            return false;
        psoDesc.VS = { g_VertexShaderBlob->GetBufferPointer(), g_VertexShaderBlob->GetBufferSize() };

        // Create the input layout
        static D3D12_INPUT_ELEMENT_DESC local_layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, IM_OFFSETOF(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, IM_OFFSETOF(ImDrawVert, uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, IM_OFFSETOF(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        psoDesc.InputLayout = { local_layout, 3 };
    }

    // Create the pixel shader
    {
        static const char* pixelShader =
			"cbuffer vertexBuffer : register(b0) \
            {\
              float4x4 ProjectionMatrix; \
            };\
            \
            cbuffer constantBuffer : register(b1) \
            {\
              float    TextureMin; \
              float    TextureMax; \
              float    TextureDepthSlice; \
              float    TextureAlpha; \
            };\
            \
            struct PS_INPUT\
            {\
              float4 pos : SV_POSITION;\
              float4 col : COLOR0;\
              float2 uv  : TEXCOORD0;\
            };\
            \
            SamplerState sampler0 : register(s0);\
            Texture2D texture0 : register(t0, space0);\
            Texture3D texture1 : register(t0, space1);\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
              float4 col = texture0.Sample(sampler0, input.uv) + texture1.Sample(sampler0, float3(input.uv, TextureDepthSlice)); \
              if (TextureAlpha != 0) \
                col = float4(col.aaa, 1); \
              else \
                { uint x,y,z = 0; texture1.GetDimensions(x,y,z); if (z > 1) col.a = 1; } \
              col = (col - TextureMin) / max(TextureMax - TextureMin, 0.0001);\
              return input.col * col; \
            }";

        // [Note] This shader is also used to render text
        // [Hack] Show alpha, otherwise discard alpha of Texture3D

        D3DCompile(pixelShader, strlen(pixelShader), NULL, NULL, NULL, "main", "ps_5_1", D3DCOMPILE_ALL_RESOURCES_BOUND, 0, &g_PixelShaderBlob, NULL);
        if (g_PixelShaderBlob == NULL)  // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
            return false;
        psoDesc.PS = { g_PixelShaderBlob->GetBufferPointer(), g_PixelShaderBlob->GetBufferSize() };
    }

    // Create the blending setup
    {
        D3D12_BLEND_DESC& desc = psoDesc.BlendState;
        desc.AlphaToCoverageEnable = false;
        desc.RenderTarget[0].BlendEnable = true;
        desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    // Create the rasterizer state
    {
        D3D12_RASTERIZER_DESC& desc = psoDesc.RasterizerState;
        desc.FillMode = D3D12_FILL_MODE_SOLID;
        desc.CullMode = D3D12_CULL_MODE_NONE;
        desc.FrontCounterClockwise = FALSE;
        desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        desc.DepthClipEnable = true;
        desc.MultisampleEnable = FALSE;
        desc.AntialiasedLineEnable = FALSE;
        desc.ForcedSampleCount = 0;
        desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    }

    // Create depth-stencil State
    {
        D3D12_DEPTH_STENCIL_DESC& desc = psoDesc.DepthStencilState;
        desc.DepthEnable = false;
        desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.StencilEnable = false;
        desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.BackFace = desc.FrontFace;
    }

    if (g_D3DDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_PipelineState)) != S_OK)
        return false;
	g_PipelineState->SetName(L"g_PipelineState");

    // Create descriptor heap - Customization
    {
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = kDescriptorSize;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (g_D3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_DescriptorHeap)) != S_OK)
			return false;

        g_DescriptorHeapIncrementSize = g_D3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_DescriptorHeap->SetName(L"g_DescriptorHeap");
    }

    // Allocate descriptor handle
    {
        IM_ASSERT(g_DescriptorHeapNextIndex == 0);

        D3D12_RESOURCE_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = 0;
        desc.Height = 0;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        int index = 0;
        index = ImGui_ImplDX12_AllocateTexture(desc);
        g_FontSrvCpuDescHandle = g_Textures[index].mCPUHandle;
        g_FontSrvGpuDescHandle = g_Textures[index].mGPUHandle;

        index = ImGui_ImplDX12_AllocateTexture(desc);
        g_Null2DSrvCpuDescHandle = g_Textures[index].mCPUHandle;
        g_Null2DSrvGpuDescHandle = g_Textures[index].mGPUHandle;

        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        index = ImGui_ImplDX12_AllocateTexture(desc);
        g_Null3DSrvCpuDescHandle = g_Textures[index].mCPUHandle;
        g_Null3DSrvGpuDescHandle = g_Textures[index].mGPUHandle;
    }

    ImGui_ImplDX12_CreateFontsTexture();

    return true;
}

void    ImGui_ImplDX12_InvalidateDeviceObjects()
{
    if (!g_D3DDevice)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (g_VertexShaderBlob) { g_VertexShaderBlob->Release(); g_VertexShaderBlob = NULL; }
    if (g_PixelShaderBlob) { g_PixelShaderBlob->Release(); g_PixelShaderBlob = NULL; }
    if (g_RootSignature) { g_RootSignature->Release(); g_RootSignature = NULL; }
    if (g_PipelineState) { g_PipelineState->Release(); g_PipelineState = NULL; }
    if (g_FontTextureResource) { g_FontTextureResource->Release(); g_FontTextureResource = NULL; io.Fonts->TexID = NULL; } // We copied g_FontTextureView to io.Fonts->TexID so let's clear that as well.
    for (UINT i = 0; i < g_NumFramesInFlight; i++)
    {
        FrameResources* fr = &g_FrameResources[i];
        if (fr->IndexBuffer)  { fr->IndexBuffer->Release();  fr->IndexBuffer = NULL; }
        if (fr->VertexBuffer) { fr->VertexBuffer->Release(); fr->VertexBuffer = NULL; }
    }

	g_FontSrvCpuDescHandle.ptr = 0;
	g_FontSrvGpuDescHandle.ptr = 0;
    if (g_DescriptorHeap) { g_DescriptorHeap->Release(); g_DescriptorHeap = NULL; }
}

bool ImGui_ImplDX12_Init(ID3D12Device* device, int num_frames_in_flight, DXGI_FORMAT rtv_format)
{
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_dx12";

    g_D3DDevice = device;
    g_RTVFormat = rtv_format;
    g_FrameResources = new FrameResources[num_frames_in_flight];
    g_NumFramesInFlight = num_frames_in_flight;
    g_FrameIndex = UINT_MAX;

    // Create buffers with a default size (they will later be grown as needed)
    for (int i = 0; i < num_frames_in_flight; i++)
    {
        FrameResources* fr = &g_FrameResources[i];
        fr->IndexBuffer = NULL;
        fr->VertexBuffer = NULL;
        fr->IndexBufferSize = 10000;
        fr->VertexBufferSize = 5000;
    }

    return true;
}

void ImGui_ImplDX12_Shutdown()
{
    ImGui_ImplDX12_InvalidateDeviceObjects();
    delete[] g_FrameResources;
    g_FrameResources = NULL;
    g_D3DDevice = NULL;
    g_NumFramesInFlight = 0;
    g_FrameIndex = UINT_MAX;
}

void ImGui_ImplDX12_NewFrame()
{
    if (!g_PipelineState)
        ImGui_ImplDX12_CreateDeviceObjects();
}

int ImGui_ImplDX12_AllocateTexture(const D3D12_RESOURCE_DESC& inDesc)
{
    Texture& texture = g_Textures[g_DescriptorHeapNextIndex];
    texture.mDesc = inDesc;

    texture.mCPUHandle = g_DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    texture.mCPUHandle.ptr += g_DescriptorHeapNextIndex * g_DescriptorHeapIncrementSize;
    texture.mGPUHandle = g_DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    texture.mGPUHandle.ptr += g_DescriptorHeapNextIndex * g_DescriptorHeapIncrementSize;

	return g_DescriptorHeapNextIndex++;
}

D3D12_CPU_DESCRIPTOR_HANDLE& ImGui_ImplDX12_TextureCPUHandle(int inIndex)
{
    return g_Textures[inIndex].mCPUHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE& ImGui_ImplDX12_TextureGPUHandle(int inIndex)
{
    return g_Textures[inIndex].mGPUHandle;
}

void ImGui_ImplDX12_ShowTextureOption(int inIndex)
{
    Texture& texture = g_Textures[inIndex];

    ImGui::PushItemWidth(100);

    ImGui::Text("Image Options");
    ImGui::SliderFloat("Min", &texture.mMin, 0.0, 1.0f); ImGui::SameLine(); ImGui::SliderFloat("Max", &texture.mMax, 0.0, 1.0f);
    ImGui::SliderInt("Depth Slice", &texture.mDepthIndex, 0, texture.mDesc.DepthOrArraySize - 1);
    ImGui::Checkbox("Show Alpha", &texture.mAlpha);

    ImGui::PopItemWidth();
}