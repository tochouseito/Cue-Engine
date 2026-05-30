#pragma once

/// ************************************************************************************
/// D3D12バックエンドの実装
/// ************************************************************************************

// === RHI includes ===
#include <RHI.h>
#include <RHICommon.h>

namespace Cue::RHI::DX12
{
    /// @brief D3D12バックエンドの実装
    class D3D12Backend final : public IRenderBackend
    {
    public:
        D3D12Backend() = default;
        ~D3D12Backend() override = default;
        Result initialize(const RenderBackendSetupInfo& a_info) override;
        Result shutdown() override;
        uint32_t width() const noexcept override { return m_width; }
        uint32_t height() const noexcept override { return m_height; }
        const uint32_t& buffer_count() const noexcept override { return m_bufferCount; }
    private:
        uint32_t m_width{};
        uint32_t m_height{};
        uint32_t m_bufferCount{};
    };
}
