#pragma once
#include "GraphicsCommon.h"
#include "ResourceHandle.h"
#include "Registry.h"

namespace Cue::GraphicsCore
{
    struct BufferDesc
    {
        std::string_view name;
        // 0 means "use the FrameGraph default buffering count".
        uint32_t bufferingCount = 0;
    };

    class IBufferManager
    {
    public:
        IBufferManager() = default;
        virtual ~IBufferManager() = default;
        virtual Result create_buffer(const BufferDesc& desc, BufferHandle& outHandle) = 0;
        virtual Result destroy_buffer(const BufferHandle& handle) = 0;
        virtual Result get_buffer(ResourceNameId nameId, uint32_t bufferIndex, BufferHandle& outHandle) = 0;
    };
} // namespace Cue::GraphicsCore
