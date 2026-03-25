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
#include "FrameGraph.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "PipelineManager.h"
#include "ViewManager.h"

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
    using PipelineHandle = Handle<PipelineTag>;
    using RootSignatureHandle = Handle<RootSignatureTag>;
    using ShaderBlobHandle = Handle<ShaderBlobTag>;

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
