// PlatformRuntimeState の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL
{
    struct PendingResizeRequest final
    {
        uint32_t width = 0;
        uint32_t height = 0;
        bool hasRequest = false;
    };

    class PlatformRuntimeState final
    {
    public:
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

        [[nodiscard]] bool has_pending_resize_request() const noexcept
        {
            return m_pendingResize.hasRequest;
        }

        [[nodiscard]] PendingResizeRequest pending_resize_request() const noexcept
        {
            return m_pendingResize;
        }

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
        PendingResizeRequest m_pendingResize{};
    };
}
