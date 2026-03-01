#pragma once
#include "GraphicsCommon.h"

namespace Cue::GraphicsCore
{
    struct BufferDesc
    {

    };

    class BufferManager
    {
    public:
        BufferManager() = default;
        virtual ~BufferManager() = default;

        virtual Result create_buffer(const BufferDesc& desc, BufferHandle& outHandle) = 0;
    private:
    };
} // namespace Cue::GraphicsCore
