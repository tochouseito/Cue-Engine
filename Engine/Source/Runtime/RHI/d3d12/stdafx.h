#pragma once

// === RHI includes ===
#include <RHICommon.h>

// === Win PAL includes ===
#include <WinPlatform.h>

// === C++ includes ===
#include <cstdint>
#include <memory>

// === DirectX12 includes ===
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#include <dxgidebug.h>
#include <d3d12sdklayers.h>

// === WRL includes ===
#include <wrl.h>

#ifndef D3D12_GPU_VIRTUAL_ADDRESS_NULL
#define D3D12_GPU_VIRTUAL_ADDRESS_NULL ((D3D12_GPU_VIRTUAL_ADDRESS)0)
#endif

#ifndef D3D12_GPU_DESCRIPTOR_HANDLE_NULL
#define D3D12_GPU_DESCRIPTOR_HANDLE_NULL (D3D12_GPU_DESCRIPTOR_HANDLE{ 0 })
#endif

#ifndef D3D12_CPU_DESCRIPTOR_HANDLE_NULL
#define D3D12_CPU_DESCRIPTOR_HANDLE_NULL (D3D12_CPU_DESCRIPTOR_HANDLE{ 0 })
#endif

namespace Cue::RHI::DX12
{
    template<typename T>
    using comPtr = Microsoft::WRL::ComPtr<T>;

    inline void set_dxgi_name([[maybe_unused]] IDXGIObject* a_obj, [[maybe_unused]] const wchar_t* a_name)
    {
        if (a_obj)
        {
            a_obj->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>((wcslen(a_name) + 1) * sizeof(wchar_t)),
                a_name);
        }
    }

    inline void set_d3d12_name([[maybe_unused]] ID3D12Object* a_obj, [[maybe_unused]] const wchar_t* a_name)
    {
        if (a_obj)
        {
            a_obj->SetName(a_name);
        }
    }

    inline D3D12_RESOURCE_STATES convert_resource_state(ResourceState state)
    {
        switch (state)
        {
        case ResourceState::Common:
            return D3D12_RESOURCE_STATE_COMMON;
        case ResourceState::CopySource:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case ResourceState::CopyDest:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case ResourceState::RenderTarget:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case ResourceState::UnorderedAccess:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case ResourceState::ShaderResource:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case ResourceState::IndirectArgument:
            return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case ResourceState::DepthWrite:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case ResourceState::Present:
            return D3D12_RESOURCE_STATE_PRESENT;
        default:
            CUE_ASSERT_MSG(false, "Invalid resource state.");
            return D3D12_RESOURCE_STATE_COMMON;
        }
    }

    inline ResourceState convert_resource_state(D3D12_RESOURCE_STATES state)
    {
        if (state & D3D12_RESOURCE_STATE_RENDER_TARGET)
        {
            return ResourceState::RenderTarget;
        }
        if (state & D3D12_RESOURCE_STATE_DEPTH_WRITE)
        {
            return ResourceState::DepthWrite;
        }
        if (state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            return ResourceState::UnorderedAccess;
        }
        if (state & (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
        {
            return ResourceState::ShaderResource;
        }
        if (state & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)
        {
            return ResourceState::IndirectArgument;
        }
        if (state & D3D12_RESOURCE_STATE_COPY_SOURCE)
        {
            return ResourceState::CopySource;
        }
        if (state & D3D12_RESOURCE_STATE_COPY_DEST)
        {
            return ResourceState::CopyDest;
        }
        if (state & D3D12_RESOURCE_STATE_PRESENT)
        {
            return ResourceState::Present;
        }
        return ResourceState::Common;
    }

    inline DXGI_FORMAT convert_color_format(ColorFormat format)
    {
        switch (format)
        {
        case ColorFormat::R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case ColorFormat::R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case ColorFormat::R32_UINT:
            return DXGI_FORMAT_R32_UINT;
        case ColorFormat::R32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case ColorFormat::D24_UNorm_S8_UInt:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }

    inline ColorFormat convert_color_format(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return ColorFormat::R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return ColorFormat::R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_R32_UINT:
            return ColorFormat::R32_UINT;
        case DXGI_FORMAT_R32_FLOAT:
            return ColorFormat::R32_FLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return ColorFormat::D24_UNorm_S8_UInt;
        default:
            CUE_ASSERT_MSG(false, "Invalid DXGI format for color format conversion.");
            return ColorFormat::R8G8B8A8_UNORM;
        }
    }

    inline D3D12_COMMAND_LIST_TYPE convert_command_list_type(CommandListType type)
    {
        switch (type)
        {
        case CommandListType::Graphics:
            return D3D12_COMMAND_LIST_TYPE_DIRECT;
        case CommandListType::Compute:
            return D3D12_COMMAND_LIST_TYPE_COMPUTE;
        case CommandListType::Copy:
            return D3D12_COMMAND_LIST_TYPE_COPY;
        default:
            CUE_ASSERT_MSG(false, "Invalid command list type.");
            return D3D12_COMMAND_LIST_TYPE_DIRECT;
        }
    }

    inline CommandListType convert_command_list_type(D3D12_COMMAND_LIST_TYPE type)
    {
        switch (type)
        {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
            return CommandListType::Graphics;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return CommandListType::Compute;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return CommandListType::Copy;
        default:
            CUE_ASSERT_MSG(false, "Invalid D3D12 command list type.");
            return CommandListType::Graphics;
        }
    }

    inline D3D12_PRIMITIVE_TOPOLOGY_TYPE convert_primitive_topology_type(PrimitiveTopologyType type)
    {
        switch (type)
        {
        case PrimitiveTopologyType::Point:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case PrimitiveTopologyType::Line:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case PrimitiveTopologyType::Triangle:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        default:
            CUE_ASSERT_MSG(false, "Invalid primitive topology type.");
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
        }
    }

    inline D3D12_PRIMITIVE_TOPOLOGY convert_primitive_topology(PrimitiveTopologyType type)
    {
        switch (type)
        {
        case PrimitiveTopologyType::Point:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case PrimitiveTopologyType::Line:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case PrimitiveTopologyType::Triangle:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        default:
            CUE_ASSERT_MSG(false, "Invalid primitive topology type.");
            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }
}
