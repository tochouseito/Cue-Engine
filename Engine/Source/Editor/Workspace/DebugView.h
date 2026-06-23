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
    private:
        RHI::DX12::D3D12Backend* m_backend = nullptr; // 非所有 backend
        RHI::ViewHandle m_debugColorSrvHandle{}; // デバッグ用カラーターゲットビュー

    };
}
