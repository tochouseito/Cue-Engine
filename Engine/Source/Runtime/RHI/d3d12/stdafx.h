#pragma once

// === RHI includes ===
#include <RHICommon.h>

// === Win PAL includes ===
#include <win/win_platform.h>

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
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    inline void SetDXGIName([[maybe_unused]] IDXGIObject* obj, [[maybe_unused]] const wchar_t* name)
    {
        if (obj)
        {
            obj->SetPrivateData(
                WKPDID_D3DDebugObjectName,
                static_cast<UINT>((wcslen(name) + 1) * sizeof(wchar_t)),
                name);
        }
    }

    inline void SetD3D12Name([[maybe_unused]] ID3D12Object* obj, [[maybe_unused]] const wchar_t* name)
    {
        if (obj)
        {
            obj->SetName(name);
        }
    }
}
