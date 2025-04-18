// dear imgui: Renderer Backend for DirectX12
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'D3D12_GPU_DESCRIPTOR_HANDLE' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Support for large meshes (64k+ vertices) with 16-bit indices.

// Important: to compile on 32-bit systems, this backend requires code to be compiled with '#define ImTextureID ImU64'.
// See imgui_impl_dx12.cpp file for details.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#pragma once

#define DXRPLAYGROUND_IMGUI

#include "../Thirdparty/imgui/imgui.h"      // IMGUI_IMPL_API
#include <dxgiformat.h>                     // DXGI_FORMAT

struct ID3D12Device;
struct ID3D12DescriptorHeap;
struct ID3D12GraphicsCommandList;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_GPU_DESCRIPTOR_HANDLE;

// cmd_list is the command list that the implementation will use to render imgui draw lists.
// Before calling the render function, caller must prepare cmd_list by resetting it and setting the appropriate
// render target and descriptor heap that contains font_srv_cpu_desc_handle/font_srv_gpu_desc_handle.
// font_srv_cpu_desc_handle and font_srv_gpu_desc_handle are handles to a single SRV descriptor to use for the internal font texture.
IMGUI_IMPL_API bool     ImGui_ImplDX12_Init(ID3D12Device* device, int num_frames_in_flight, DXGI_FORMAT rtv_format, ID3D12DescriptorHeap* cbv_srv_heap,
                                            D3D12_CPU_DESCRIPTOR_HANDLE font_srv_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE font_srv_gpu_desc_handle);
IMGUI_IMPL_API void     ImGui_ImplDX12_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplDX12_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplDX12_RenderDrawData(ImDrawData* draw_data, ID3D12GraphicsCommandList* graphics_command_list);

// Use if you want to reset your rendering device without losing Dear ImGui state.
IMGUI_IMPL_API void     ImGui_ImplDX12_InvalidateDeviceObjects();
IMGUI_IMPL_API bool     ImGui_ImplDX12_CreateDeviceObjects();

#ifdef DXRPLAYGROUND_IMGUI
struct ID3D12Resource;
struct D3D12_SHADER_RESOURCE_VIEW_DESC;
extern void (*ImGui_ImplDX12_CreateShaderResourceViewCallback)(ID3D12Resource* resource, D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
struct ImGui_ImplDX12_ShaderContantsType
{
    float mMin = 0.0f;
    float mMax = 1.0f;
    float mSlice = 0.0f;
    float mAlpha = 0.0f;
};
extern ImGui_ImplDX12_ShaderContantsType ImGui_ImplDX12_ShaderContants;
extern ImTextureID ImGui_ImplDX12_FontTextureID;
extern ImTextureID ImGui_ImplDX12_NullTexture2D;
extern ImTextureID ImGui_ImplDX12_NullTexture3D;
enum
{
    ImGui_ImplDX12_ImTextureID_Mask_3D  = 0x1,
};

constexpr unsigned int ImGuiSelectableFlags_SelectOnNav = 1 << 21;  // from imgui_internal.h
#endif // DXRPLAYGROUND_IMGUI