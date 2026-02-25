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

        //const BufferRecord* get_buffer(BufferHandle handle) const noexcept
        //{
        //    // 1) ハンドルが有効なら参照先を返し、無効なら nullptr を返す。
        //    const BufferRecord* record = nullptr;
        //    (void)m_bufferRegistry.with(
        //        handle,
        //        [&record](const BufferRecord& value)
        //        {
        //            record = &value;
        //        });

        //    return record;
        //}
    private:
        BufferRegistry m_bufferRegistry;
    };
} // namespace Cue::GraphicsCore
