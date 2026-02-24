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
        ~BufferManager() = default;

        BufferHandle create_buffer(const BufferDesc& desc)
        {

        }

        const BufferRecord& get_buffer(BufferHandle handle) const noexcept
        {
            
        }
    private:
        BufferRegistry m_bufferRegistry;
    };
}
