#pragma once

/// **********************************************************************
/// GameView
/// **********************************************************************

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === D3D12 include ===
#include <D3D12Backend.h>

// === C++ includes ===
#include <cstdint>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    /// @brief FinalColor を Editor 上に表示するゲーム画面ビュー
    class GameView final
    {
    public:
        explicit GameView(RHI::DX12::D3D12Backend* a_backend) noexcept
            : m_backend(a_backend)
        {}

        /// @brief 更新処理
        void update();

    private:
        /// @brief 利用可能領域へ render target をアスペクト維持で収める
        [[nodiscard]] static ImVec2 calculate_fit_size(ImVec2 a_availableRegion,
                                                       uint32_t a_textureWidth,
                                                       uint32_t a_textureHeight) noexcept;

        RHI::DX12::D3D12Backend* m_backend = nullptr; // 非所有 backend
        RHI::ViewHandle m_finalColorSrvHandle{};      // GameView に表示する FinalColor SRV
    };
} // namespace Cue::Editor
