// EffectPreviewView の役割と公開要素を定義する

#pragma once

// === D3D12 includes ===
#include <D3D12Backend.h>

// === C++ includes ===
#include <cstdint>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class EffectPreviewView final
    {
    public:
        explicit EffectPreviewView(RHI::DX12::D3D12Backend* a_backend)
            : m_backend(a_backend)
        {}

        ~EffectPreviewView() = default;

        [[nodiscard]] bool draw(const ImVec2& a_size)
        {
            if (m_backend == nullptr || m_backend->get_view_manager() == nullptr)
            {
                return false;
            }

            Result viewResult =
                m_backend->get_view_manager()->get_view(
                    "EffectPreviewColorSRV",
                    m_colorSrvHandle);
            if (!viewResult || !m_colorSrvHandle.valid())
            {
                return false;
            }

            const D3D12_GPU_DESCRIPTOR_HANDLE srvGpuDescHandle =
                m_backend->get_gpu_descriptor_handle(
                    m_colorSrvHandle,
                    m_backend->current_back_buffer_index(),
                    m_backend->buffer_count());
            if (srvGpuDescHandle.ptr == 0 || a_size.x <= 0.0f ||
                a_size.y <= 0.0f)
            {
                return false;
            }

            ImGui::Image(
                static_cast<ImTextureID>(srvGpuDescHandle.ptr),
                a_size);
            return true;
        }

    private:
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        RHI::ViewHandle m_colorSrvHandle{};
    };
} // namespace Cue::Editor
