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

        virtual BufferHandle create_buffer(const BufferDesc& desc) = 0;

    private:
    };
} // namespace Cue::GraphicsCore
