#pragma once

// === C++ standard library includes ===
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
// Container
#include <vector>
#include <unordered_map>
#include <deque>

// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

// === Math includes ===
#include <CueMath.h>

// === Core includes ===
#include <Logger.h>
#include <Pool.h>
#include <Native/Handle.h>
#include <Registry.h>

namespace Cue::GraphicsCore
{
    template <class Tag>
    using Handle = Core::Handle<Tag>;

    template <class Tag, class Record>
    using Registry = Core::Registry<Tag, Record>;

    using Core::ResourceNameId;

    struct BufferTag {};
    struct TextureTag {};
    struct PipelineStateTag {};
    struct RootSignatureTag {};
    struct ShaderBlobTag {};
    struct ViewTag {};

    using BufferHandle = Handle<BufferTag>;
    using TextureHandle = Handle<TextureTag>;
    using PipelineStateHandle = Handle<PipelineStateTag>;
    using RootSignatureHandle = Handle<RootSignatureTag>;
    using ShaderBlobHandle = Handle<ShaderBlobTag>;
    using ViewHandle = Handle<ViewTag>;

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

    enum class ResourceHeapType : uint8_t
    {
        Default,
        Upload,
    };

    inline const char* resource_heap_type_to_string(ResourceHeapType type) noexcept
    {
        switch (type)
        {
        case ResourceHeapType::Default: return "Default";
        case ResourceHeapType::Upload: return "Upload";
        default: return "Unknown";
        }
    }

    enum class CommandListType : uint8_t
    {
        Graphics,
        Compute,
        Copy
    };

    inline const char* command_list_type_to_string(CommandListType type) noexcept
    {
        switch (type)
        {
        case CommandListType::Graphics: return "Graphics";
        case CommandListType::Compute: return "Compute";
        case CommandListType::Copy: return "Copy";
        default: return "Unknown";
        }
    }

    enum class ResourceAccessType : uint8_t
    {
        Read,
        Write,
    };

    inline const char* resource_access_type_to_string(ResourceAccessType type) noexcept
    {
        switch (type)
        {
        case ResourceAccessType::Read: return "Read";
        case ResourceAccessType::Write: return "Write";
        default: return "Unknown";
        }
    }

    enum class ResourceKind : uint8_t
    {
        Buffer,
        Texture,
    };

    enum class ResourceInstanceSource : uint8_t
    {
        Fixed,
        FrameResourceIndex,
        SwapchainImageIndex,
    };

    inline const char* resource_kind_to_string(ResourceKind kind) noexcept
    {
        switch (kind)
        {
        case ResourceKind::Buffer: return "Buffer";
        case ResourceKind::Texture: return "Texture";
        default: return "Unknown";
        }
    }

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

    enum class TextureDimension : uint8_t
    {
        Texture2D,
        Texture3D,
    };

    inline const char* texture_dimension_to_string(TextureDimension dim) noexcept
    {
        switch (dim)
        {
        case TextureDimension::Texture2D: return "Texture2D";
        case TextureDimension::Texture3D: return "Texture3D";
        default: return "Unknown";
        }
    }
}
