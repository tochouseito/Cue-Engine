#pragma once

// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

// === Math includes ===
#include <CueMath.h>

// === Core includes ===
#include <Native/Handle.h>
#include <Native/EngineNativeStruct.h>
#include <IO/Logger.h>
#include <Container/Pool.h>
#include <Container/Registry.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <string_view>

// === RHI includes ===
#include "BackendFactory.h"
#include "Interfaces.h"

namespace Cue::RHI
{
    template<class Tag>
    using Handle = Core::Handle<Tag>;

    template<class Tag, class RecordType>
    using Registry = Core::Registry<Tag, RecordType>;

    using ResourceNameId = Core::ResourceNameId;

    struct BufferTag {};
    struct TextureTag {};
    struct ViewTag {};
    struct PipelineTag {};
    struct RootSignatureTag {};
    struct ShaderBlobTag {};
    struct StaticMeshTag {};

    using BufferHandle = Handle<BufferTag>;
    using TextureHandle = Handle<TextureTag>;
    using ViewHandle = Handle<ViewTag>;
    using PipelineStateHandle = Handle<PipelineTag>;
    using RootSignatureHandle = Handle<RootSignatureTag>;
    using ShaderBlobHandle = Handle<ShaderBlobTag>;
    using StaticMeshHandle = Handle<StaticMeshTag>;

    enum class ColorFormat : uint8_t
    {
        R8G8B8A8_UNORM,
        R8G8B8A8_UNORM_SRGB,
        D24_UNorm_S8_UInt
    };

    inline const char* color_format_to_string(ColorFormat format) noexcept
    {
        switch (format)
        {
        case ColorFormat::R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case ColorFormat::R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case ColorFormat::D24_UNorm_S8_UInt: return "D24_UNorm_S8_UInt";
        default: return "Unknown";
        }
    }

    enum class BufferKind : uint8_t
    {
        Texture,
        Buffer,
        RenderTarget,
        DepthStencil
    };

    enum class BufferType : uint8_t
    {
        Vertex,
        Index,
        Constant,
        Structured,
        UnorderedAccess,
        Raw,
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
