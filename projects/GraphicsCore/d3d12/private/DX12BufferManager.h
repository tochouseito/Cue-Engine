#pragma once
#include "stdafx.h"
#include "GpuBuffer.h"
#include <BufferManager.h>

namespace Cue::GraphicsCore::DX12
{
    class DX12BufferManager final : public BufferManager
    {
    public:
        DX12BufferManager() = default;
        ~DX12BufferManager() override = default;

        BufferHandle create_buffer(const BufferDesc& desc) override
        {
            // 1) 現状MVPでは空レコードを登録してハンドルだけ発行する。
            (void)desc;
            GpuBufferResource resource; // 仮のGpuBufferResourceクラス
            return m_bufferRegistry.create(resource);
        }
    private:
        Registry<BufferTag, GpuBufferResource> m_bufferRegistry;
    };

} // namespace Cue::GraphicsCore::DX12
