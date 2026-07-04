#pragma once

/// **********************************************************************
/// DebugView
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>
#include <CueAssert.h>

// === D3D12 include ===
#include <D3D12Backend.h>

// === Editor include ===

// === C++ includes ===
#include <algorithm>
#include <cstdint>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class DebugView final
    {
    public:
        DebugView(RHI::DX12::D3D12Backend* a_backend) noexcept
            : m_backend(a_backend)
        {}

        /// @brief 更新処理
        void update();

        /// @brief 表示領域の幅
        [[nodiscard]] uint32_t viewport_width() const noexcept
        {
            return m_viewportWidth;
        }

        /// @brief 表示領域の高さ
        [[nodiscard]] uint32_t viewport_height() const noexcept
        {
            return m_viewportHeight;
        }

        /// @brief mouse が表示領域上にあるか
        [[nodiscard]] bool is_viewport_hovered() const noexcept
        {
            return m_isViewportHovered;
        }

        /// @brief DebugView window が focused か
        [[nodiscard]] bool is_focused() const noexcept
        {
            return m_isFocused;
        }

    private:
        RHI::DX12::D3D12Backend* m_backend = nullptr; // 非所有 backend
        RHI::ViewHandle m_finalColorSrvHandle{}; // 表示用カラーターゲットビュー
        uint32_t m_viewportWidth = 1; // DebugCamera の projection に使う表示幅
        uint32_t m_viewportHeight = 1; // DebugCamera の projection に使う表示高
        bool m_isViewportHovered = false; // DebugCamera 操作開始判定用
        bool m_isFocused = false; // DebugCamera keyboard 入力判定用

    };
}
