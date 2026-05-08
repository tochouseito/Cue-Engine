#pragma once

// === RHI includes ===
#include "BackendFactory.h"
#include "Interfaces.h"

namespace Cue::RHI
{
    enum class ColorFormat : uint8_t
    {
        R8G8B8A8_UNORM,
        R8G8B8A8_UNORM_SRGB,
        R32_UINT,
        R32_FLOAT,
        D24_UNorm_S8_UInt
    };

    inline const char* color_format_to_string(ColorFormat format) noexcept
    {
        switch (format)
        {
        case ColorFormat::R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case ColorFormat::R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case ColorFormat::R32_UINT: return "R32_UINT";
        case ColorFormat::R32_FLOAT: return "R32_FLOAT";
        case ColorFormat::D24_UNorm_S8_UInt: return "D24_UNorm_S8_UInt";
        default: return "Unknown";
        }
    }

    inline uint32_t color_format_byte_size(ColorFormat format) noexcept
    {
        switch (format)
        {
        case ColorFormat::R8G8B8A8_UNORM:
        case ColorFormat::R8G8B8A8_UNORM_SRGB:
        case ColorFormat::R32_UINT:
        case ColorFormat::R32_FLOAT:
            return 4;
        default:
            return 0;
        }
    }

    enum class BufferKind : uint8_t
    {
        Texture,
        Buffer,
    };

    enum class BufferType : uint8_t
    {
        Vertex,
        Index,
        Constant,
        Structured,
        UnorderedAccess,
        Raw,
        Readback,
        Unknown,
    };

    inline const char* buffer_type_to_string(BufferType type) noexcept
    {
        switch (type)
        {
        case BufferType::Vertex: return "Vertex";
        case BufferType::Index: return "Index";
        case BufferType::Constant: return "Constant";
        case BufferType::Structured: return "Structured";
        case BufferType::UnorderedAccess: return "UnorderedAccess";
        case BufferType::Raw: return "Raw";
        case BufferType::Readback: return "Readback";
        case BufferType::Unknown: return "Unknown";
        default: return "Unknown";
        }
    }

    enum class ViewType : uint8_t
    {
        ConstantBuffer,
        ShaderResourceBuffer,
        ShaderResourceRawBuffer,
        UnorderedAccessBuffer,
        UnorderedAccessRawBuffer,
        ShaderResourceTexture2D,
        UnorderedAccessTexture2D,
        RenderTarget,
        DepthStencil,
    };

    enum class IndexFormat : uint8_t
    {
        UInt16,
        UInt32,
    };

    inline const char* index_format_to_string(IndexFormat format) noexcept
    {
        switch (format)
        {
        case IndexFormat::UInt16: return "UInt16";
        case IndexFormat::UInt32: return "UInt32";
        default: return "Unknown";
        }
    }
}
