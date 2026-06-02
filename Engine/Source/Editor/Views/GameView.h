// GameView の役割と公開要素を定義する

#pragma once

// === D3D12 includes ===
#include <D3D12Backend.h>

// === C++ includes ===
#include <cstdint>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class GameView final
    {
    public:
        explicit GameView(RHI::DX12::D3D12Backend* a_backend)
            : m_backend(a_backend)
        {
        }

        ~GameView() = default;

        void update()
        {
            if (m_backend == nullptr || m_backend->get_view_manager() == nullptr)
            {
                return;
            }

            Result viewResult =
                m_backend->get_view_manager()->get_view("GameColorSRV",
                    m_gameColorSrvHandle);
            if (!viewResult)
            {
                CUE_ASSERTF(false,
                    "Failed to get view: %s (code: %s, severity: %s) at %s:%u in function %s",
                    viewResult.message.data(), Cue::to_string(viewResult.code),
                    Cue::to_string(viewResult.severity), viewResult.file,
                    viewResult.line, viewResult.function);
            }

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            const ImGuiWindowFlags windowFlags =
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::Begin("GameView", nullptr, windowFlags);
            draw_view_texture(m_gameColorSrvHandle);
            ImGui::End();
            ImGui::PopStyleVar();
        }

    private:
        void draw_view_texture(RHI::ViewHandle a_srvHandle)
        {
            const D3D12_GPU_DESCRIPTOR_HANDLE srvGpuDescHandle =
                m_backend->get_gpu_descriptor_handle(
                    a_srvHandle,
                    m_backend->current_back_buffer_index(),
                    m_backend->buffer_count());
            const uint32_t colorWidth = m_backend->width();
            const uint32_t colorHeight = m_backend->height();
            if (srvGpuDescHandle.ptr == 0 || colorWidth == 0 || colorHeight == 0)
            {
                return;
            }

            const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
            float displayWidth = availableRegion.x;
            float displayHeight =
                displayWidth * static_cast<float>(colorHeight) /
                static_cast<float>(colorWidth);

            if (availableRegion.y > 0.0f && displayHeight > availableRegion.y)
            {
                const float scale = availableRegion.y / displayHeight;
                displayWidth *= scale;
                displayHeight *= scale;
            }

            if (displayWidth <= 0.0f || displayHeight <= 0.0f)
            {
                return;
            }

            const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(
                cursorPos.x + (availableRegion.x - displayWidth) * 0.5f,
                cursorPos.y + (availableRegion.y - displayHeight) * 0.5f));

            ImGui::Image(
                static_cast<ImTextureID>(srvGpuDescHandle.ptr),
                ImVec2(displayWidth, displayHeight));
        }

    private:
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        RHI::ViewHandle m_gameColorSrvHandle{};
    };
}
