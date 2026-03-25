#pragma once

// === RHI Includes ===
#include <BufferManager.h>

namespace Cue::RHI::DX12
{
    class DX12BufferManager final : public IBufferManager
    {
    public:
        DX12BufferManager() = default;
        ~DX12BufferManager() override = default;
        Result create_buffer(const BufferDesc& desc, BufferHandle& out) override;
        Result destroy_buffer(BufferHandle handle) override;
    };
}
