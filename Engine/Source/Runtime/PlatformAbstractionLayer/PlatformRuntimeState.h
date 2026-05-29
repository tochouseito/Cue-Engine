#pragma once

/// ********************************************************************************
/// プラットフォームランタイム状態
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL
{
    /// @brief 保留中のウィンドウリサイズ要求
    struct PendingResizeRequest final
    {
        uint32_t width = 0;
        uint32_t height = 0;
        bool hasRequest = false;
    };

    class PlatformRuntimeState final
    {
    public:

        /// @brief ウィンドウリサイズ要求を処理
        /// @param a_width リクエストされたウィンドウ幅
        /// @param a_height リクエストされたウィンドウ高さ
        /// @return リクエストの結果
        Result request_window_resize(uint32_t a_width, uint32_t a_height)
        {
            if (a_width == 0 || a_height == 0)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Window resize request must have non-zero width and height.");
            }

            // 実リサイズは別タスクで扱うため、ここでは最新要求だけ保持する
            m_pendingResize.width = a_width;
            m_pendingResize.height = a_height;
            m_pendingResize.hasRequest = true;
            return Result::ok();
        }

        /// @brief 保留中のウィンドウリサイズ要求があるか
        /// @return 保留中のリサイズ要求がある場合は true、それ以外の場合は false
        [[nodiscard]] bool has_pending_resize_request() const noexcept
        {
            return m_pendingResize.hasRequest;
        }
        
        /// @brief 保留中のウィンドウリサイズ要求を取得
        /// @return 保留中のリサイズ要求
        [[nodiscard]] PendingResizeRequest pending_resize_request() const noexcept
        {
            return m_pendingResize;
        }
        
        /// @brief 保留中のウィンドウリサイズ要求を消費
        /// @param a_outRequest 消費されたリサイズ要求の出力先
        /// @return 保留中のリサイズ要求が存在した場合は true、それ以外の場合は false
        bool consume_pending_resize_request(PendingResizeRequest& a_outRequest) noexcept
        {
            if (!m_pendingResize.hasRequest)
            {
                return false;
            }

            a_outRequest = m_pendingResize;
            m_pendingResize = PendingResizeRequest{};
            return true;
        }

    private:
        PendingResizeRequest m_pendingResize{}; // 保留中のウィンドウリサイズ要求
    };
}
