#pragma once

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Editor includes ===
#include "DebugCamera.h"

// === C++ includes ===
#include <cstdint>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class DebugView final
    {
    public:
        struct PickRequest final
        {
            float normalizedX = 0.0f;
            float normalizedY = 0.0f;
        };

        DebugView(
            RHI::DX12::D3D12Backend* a_backend,
            DebugCamera* a_camera)
            : m_backend(a_backend)
            , m_camera(a_camera)
        {
        }
        ~DebugView() = default;

        [[nodiscard]] bool consume_pick_request(
            PickRequest& a_outRequest) noexcept
        {
            if (!m_hasPickRequest)
            {
                return false;
            }

            a_outRequest = m_pickRequest;
            m_hasPickRequest = false;
            return true;
        }

        void update()
        {
            if (m_backend == nullptr || m_backend->get_view_manager() == nullptr)
            {
                return;
            }

            Result viewResult =
                m_backend->get_view_manager()->get_view("DebugColorSRV",
                    m_debugColorSrvHandle);
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
                ImGuiWindowFlags_MenuBar |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::Begin("DebugView", nullptr, windowFlags);

            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("Test"))
                {
                    ImGui::MenuItem("DebugView Test", nullptr, false, false);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
            if (m_camera != nullptr && availableRegion.x > 0.0f &&
                availableRegion.y > 0.0f)
            {
                m_camera->set_aspect(availableRegion.x / availableRegion.y);
                m_camera->update(ImGui::IsWindowHovered());
            }

            D3D12_GPU_DESCRIPTOR_HANDLE debugColorSrvGpuDescHandle =
                m_backend->get_gpu_descriptor_handle(
                    m_debugColorSrvHandle,
                    m_backend->current_back_buffer_index(),
                    m_backend->buffer_count());
            const uint32_t debugColorWidth = m_backend->width();
            const uint32_t debugColorHeight = m_backend->height();
            if (debugColorSrvGpuDescHandle.ptr != 0 &&
                debugColorWidth > 0 && debugColorHeight > 0 &&
                availableRegion.x > 0.0f && availableRegion.y > 0.0f)
            {
                ImGui::Image(
                    static_cast<ImTextureID>(debugColorSrvGpuDescHandle.ptr),
                    availableRegion);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    const ImVec2 mousePos = ImGui::GetMousePos();
                    const ImVec2 itemMin = ImGui::GetItemRectMin();
                    const ImVec2 itemMax = ImGui::GetItemRectMax();
                    const float itemWidth = itemMax.x - itemMin.x;
                    const float itemHeight = itemMax.y - itemMin.y;
                    if (itemWidth > 0.0f && itemHeight > 0.0f)
                    {
                        m_pickRequest.normalizedX =
                            (mousePos.x - itemMin.x) / itemWidth;
                        m_pickRequest.normalizedY =
                            (mousePos.y - itemMin.y) / itemHeight;
                        m_hasPickRequest = true;
                    }
                }
            }

            ImGui::End();
            ImGui::PopStyleVar();
        }
    private:
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        DebugCamera* m_camera = nullptr;
        RHI::ViewHandle m_debugColorSrvHandle{};
        PickRequest m_pickRequest{};
        bool m_hasPickRequest = false;
    };
}
