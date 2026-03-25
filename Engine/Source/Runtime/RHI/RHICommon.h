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
}
