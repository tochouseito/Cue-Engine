#pragma once

/// ********************************************************************************
/// プラットフォームコマンドコンテキスト
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === PAL includes ===
#include <PlatformCommands.h>
#include <PlatformRuntimeState.h>

// === Engine includes ===
#include <FrameController.h>

namespace Cue
{
    /// @brief プラットフォームコマンドコンテキスト
    class PlatformCommandContext final : public PAL::IPlatformCommandContext
    {
    public:
        PlatformCommandContext(PAL::PlatformRuntimeState& a_state, FrameController* a_frameController) noexcept;

        /// @brief ウィンドウリサイズ要求を処理
        /// @param a_width リクエストされたウィンドウ幅
        /// @param a_height リクエストされたウィンドウ高さ
        /// @return リクエストの結果
        Result request_window_resize(uint32_t a_width, uint32_t a_height) override;

    private:
        PAL::PlatformRuntimeState& m_state;
        FrameController* m_frameController = nullptr;
    };
}
