#pragma once

// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

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

    using BufferHandle = Handle<BufferTag>;
    using TextureHandle = Handle<TextureTag>;
    using ViewHandle = Handle<ViewTag>;
    using PipelineStateHandle = Handle<PipelineTag>;
    using RootSignatureHandle = Handle<RootSignatureTag>;
    using ShaderBlobHandle = Handle<ShaderBlobTag>;

    enum class ColorFormat : uint8_t
    {
        R8G8B8A8_UNORM,
        R8G8B8A8_UNORM_SRGB,
    };

    inline const char* color_format_to_string(ColorFormat format) noexcept
    {
        switch (format)
        {
        case ColorFormat::R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case ColorFormat::R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        default: return "Unknown";
        }
    }

    enum class DSVFormat : uint8_t
    {
        D24_UNorm_S8_UInt,
    };

    inline const char* dsv_format_to_string(DSVFormat format) noexcept
    {
        switch (format)
        {
        case DSVFormat::D24_UNorm_S8_UInt: return "D24_UNorm_S8_UInt";
        default: return "Unknown";
        }
    }

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

    enum class ResourceState : uint8_t
    {
        Common,
        CopySource,
        CopyDest,
        RenderTarget,
        UnorderedAccess,
        ShaderResource,
        DepthWrite,
        Present
    };

    inline const char* resource_state_to_string(ResourceState state) noexcept
    {
        switch (state)
        {
        case ResourceState::Common: return "Common";
        case ResourceState::RenderTarget: return "RenderTarget";
        case ResourceState::UnorderedAccess: return "UnorderedAccess";
        case ResourceState::ShaderResource: return "ShaderResource";
        case ResourceState::DepthWrite: return "DepthWrite";
        case ResourceState::Present: return "Present";
        default: return "Unknown";
        }
    }
}
