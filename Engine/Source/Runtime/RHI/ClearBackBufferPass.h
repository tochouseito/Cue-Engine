#pragma once

// === RHI includes ===
#include "FrameGraph.h"

// === C++ includes ===
#include <array>
#include <string>

namespace Cue::RHI
{
    class ClearBackBufferPass final : public FrameGraphPass
    {
    public:
        explicit ClearBackBufferPass(
            std::string_view name = "ClearBackBuffer",
            const std::array<float, 4>& clearColor = { 0.5f, 0.0f, 0.0f, 1.0f },
            ColorFormat colorFormat = ColorFormat::R8G8B8A8_UNORM) :
            m_name(name),
            m_clearColor(clearColor),
            m_colorFormat(colorFormat)
        {
        }

        std::string_view name() const noexcept override
        {
            return m_name;
        }

        CommandListType type() const noexcept override
        {
            return CommandListType::Graphics;
        }

        Result setup(FrameGraphBuilder& builder) override;
        Result execute(FrameGraphPassContext& context) override;
        bool side_effect() const noexcept override
        {
            return true;
        }

    private:
        std::string m_name = {};
        std::array<float, 4> m_clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        ColorFormat m_colorFormat = ColorFormat::R8G8B8A8_UNORM;
        FrameGraphViewRef m_renderTargetView = {};
    };
}
