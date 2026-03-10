#pragma once
#include "cqrs.h"
#include <GraphicsCore.h>

namespace Cue::CQRS::Commands
{
    class EngineCommandContext final : public ICommandContext
    {
    public:
        void set_graphics_backend(GraphicsCore::Backend* graphicsBackend) noexcept
        {
            // 1) command 実装が backend 協力者へ到達できるよう、Engine 初期化時に注入する。
            m_graphicsBackend = graphicsBackend;
        }

        [[nodiscard]] GraphicsCore::Backend* graphics_backend() const noexcept
        {
            return m_graphicsBackend;
        }

    private:
        GraphicsCore::Backend* m_graphicsBackend = nullptr;
    };
}
