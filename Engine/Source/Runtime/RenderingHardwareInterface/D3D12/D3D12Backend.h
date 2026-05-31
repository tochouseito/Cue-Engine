#pragma once

/// ************************************************************************************
/// D3D12バックエンドの実装
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === RHI includes ===
#include <RHI.h>
#include <RHICommon.h>

// === D3D12 includes ===
#include "ResourceLeakChecker.h"
#include "HLSLCompiler.h"
#include "DX12RenderDevice.h"
#include "DescriptorAllocator.h"

namespace Cue::RHI::DX12
{
    /// @brief D3D12バックエンドの実装
    class D3D12Backend final : public IRenderBackend
    {
    public:
        D3D12Backend();
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
        std::unique_ptr<ResourceLeakChecker> m_resourceLeakChecker = std::make_unique<ResourceLeakChecker>();
        std::unique_ptr<HLSLCompiler> m_hlslCompiler = std::make_unique<HLSLCompiler>();
        std::unique_ptr<DX12RenderDevice> m_renderDevice = nullptr;
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr;
    };
}
