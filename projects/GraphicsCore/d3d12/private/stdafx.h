#pragma once

// マクロ定義
#define WIN32_LEAN_AND_MEAN             // Windows ヘッダーからあまり使われない部分を除外する
#define NOMINMAX                        // min と max マクロの定義を防止する

// c++ 標準ライブラリ include
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <unordered_map>

// directx 関連 include
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#ifndef CUE_RELEASE
#include <dxgidebug.h>
#include <d3d12sdklayers.h>
#endif

// microsoft wrl 関連 include
#include <wrl.h>

// base 関連 include
#include <Result.h>
#include <CueAssert.h>

// core 関連 include
#include <Logger.h>

// math 関連 include
#include <CueMath.h>

// graphics core 関連 include
#include <GraphicsCommon.h>
#include <GraphicsInterface.h>

#ifndef D3D12_GPU_VIRTUAL_ADDRESS_NULL
#define D3D12_GPU_VIRTUAL_ADDRESS_NULL ((D3D12_GPU_VIRTUAL_ADDRESS)0)
#endif

#ifndef D3D12_GPU_DESCRIPTOR_HANDLE_NULL
#define D3D12_GPU_DESCRIPTOR_HANDLE_NULL (D3D12_GPU_DESCRIPTOR_HANDLE{ 0 })
#endif

#ifndef D3D12_CPU_DESCRIPTOR_HANDLE_NULL
#define D3D12_CPU_DESCRIPTOR_HANDLE_NULL (D3D12_CPU_DESCRIPTOR_HANDLE{ 0 })
#endif

namespace Cue::GraphicsCore::DX12
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

    std::string to_utf8(std::wstring_view w) noexcept;
    std::wstring to_utf16(std::string_view s) noexcept;

    DXGI_FORMAT convert_color_format(Cue::GraphicsCore::ColorFormat format);
}
