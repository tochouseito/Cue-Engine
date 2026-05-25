#pragma once

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Editor includes ===
#include "DebugCamera.h"

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
        using DrawAddMenuCallback = void (*)(void* a_context);
        using DrawOverlayCallback = bool (*)(
            void* a_context,
            const ImVec2& a_viewportMin,
            const ImVec2& a_viewportMax,
            ImDrawList* a_drawList);
        using DrawSceneMenuCallback = void (*)(void* a_context);
        using DrawViewMenuCallback = void (*)(void* a_context);

        struct PickRequest final
        {
            float normalizedX = 0.0f;
            float normalizedY = 0.0f;
            uint32_t pixelX = 0;
            uint32_t pixelY = 0;
        };

        DebugView(
            RHI::DX12::D3D12Backend* a_backend,
            DebugCamera* a_camera)
            : m_backend(a_backend)
            , m_camera(a_camera)
        {
        }
        ~DebugView() = default;

        void set_add_menu_callback(
            void* a_context,
            DrawAddMenuCallback a_callback) noexcept
        {
            m_addMenuContext = a_context;
            m_drawAddMenuCallback = a_callback;
        }

        void set_overlay_callback(
            void* a_context,
            DrawOverlayCallback a_callback) noexcept
        {
            m_overlayContext = a_context;
            m_drawOverlayCallback = a_callback;
        }

        void set_view_menu_callback(
            void* a_context,
            DrawViewMenuCallback a_callback) noexcept
        {
            m_viewMenuContext = a_context;
            m_drawViewMenuCallback = a_callback;
        }

        void set_scene_menu_callback(
            void* a_context,
            DrawSceneMenuCallback a_callback) noexcept
        {
            m_sceneMenuContext = a_context;
            m_drawSceneMenuCallback = a_callback;
        }

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

        void clear_pick_request() noexcept
        {
            m_hasPickRequest = false;
        }

        [[nodiscard]] bool viewport_rect(
            ImVec2& a_outMin,
            ImVec2& a_outMax) const noexcept
        {
            if (!m_hasViewportRect)
            {
                return false;
            }

            a_outMin = m_viewportMin;
            a_outMax = m_viewportMax;
            return true;
        }

        void update()
        {
            m_hasViewportRect = false;
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
            const bool isVisible =
                ImGui::Begin("DebugView", nullptr, windowFlags);
            if (!isVisible)
            {
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }

            if (ImGui::BeginMenuBar())
            {
                if (m_drawAddMenuCallback != nullptr &&
                    ImGui::BeginMenu("追加"))
                {
                    m_drawAddMenuCallback(m_addMenuContext);
                    ImGui::EndMenu();
                }

                if (m_drawViewMenuCallback != nullptr &&
                    ImGui::BeginMenu("ビュー"))
                {
                    m_drawViewMenuCallback(m_viewMenuContext);
                    ImGui::EndMenu();
                }

                if (m_drawSceneMenuCallback != nullptr &&
                    ImGui::BeginMenu("シーン"))
                {
                    m_drawSceneMenuCallback(m_sceneMenuContext);
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Test"))
                {
                    ImGui::MenuItem("DebugView Test", nullptr, false, false);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
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
                if (availableRegion.x <= 0.0f || availableRegion.y <= 0.0f)
                {
                    ImGui::End();
                    ImGui::PopStyleVar();
                    return;
                }

                if (m_camera != nullptr)
                {
                    m_camera->set_aspect(availableRegion.x / availableRegion.y);
                    m_camera->update(ImGui::IsWindowHovered());
                }

                ImGui::Image(
                    static_cast<ImTextureID>(debugColorSrvGpuDescHandle.ptr),
                    availableRegion);
                m_viewportMin = ImGui::GetItemRectMin();
                m_viewportMax = ImGui::GetItemRectMax();
                m_hasViewportRect = true;
                const bool isImageClicked =
                    ImGui::IsItemClicked(ImGuiMouseButton_Left);
                bool isOverlayBlockingPick = false;
                if (m_drawOverlayCallback != nullptr)
                {
                    isOverlayBlockingPick = m_drawOverlayCallback(
                        m_overlayContext,
                        m_viewportMin,
                        m_viewportMax,
                        ImGui::GetWindowDrawList());
                }
                if (!isOverlayBlockingPick && isImageClicked)
                {
                    const ImVec2 mousePos = ImGui::GetMousePos();
                    const ImVec2 itemMin = m_viewportMin;
                    const ImVec2 itemMax = m_viewportMax;
                    const float itemWidth = itemMax.x - itemMin.x;
                    const float itemHeight = itemMax.y - itemMin.y;
                    if (itemWidth > 0.0f && itemHeight > 0.0f)
                    {
                        m_pickRequest.normalizedX = std::clamp(
                            (mousePos.x - itemMin.x) / itemWidth,
                            0.0f,
                            1.0f);
                        m_pickRequest.normalizedY = std::clamp(
                            (mousePos.y - itemMin.y) / itemHeight,
                            0.0f,
                            1.0f);
                        m_pickRequest.pixelX = (std::min)(
                            static_cast<uint32_t>(
                                m_pickRequest.normalizedX *
                                static_cast<float>(debugColorWidth)),
                            debugColorWidth - 1u);
                        m_pickRequest.pixelY = (std::min)(
                            static_cast<uint32_t>(
                                m_pickRequest.normalizedY *
                                static_cast<float>(debugColorHeight)),
                            debugColorHeight - 1u);
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
        ImVec2 m_viewportMin = ImVec2(0.0f, 0.0f);
        ImVec2 m_viewportMax = ImVec2(0.0f, 0.0f);
        void* m_addMenuContext = nullptr;
        void* m_overlayContext = nullptr;
        void* m_sceneMenuContext = nullptr;
        void* m_viewMenuContext = nullptr;
        DrawAddMenuCallback m_drawAddMenuCallback = nullptr;
        DrawOverlayCallback m_drawOverlayCallback = nullptr;
        DrawSceneMenuCallback m_drawSceneMenuCallback = nullptr;
        DrawViewMenuCallback m_drawViewMenuCallback = nullptr;
        bool m_hasPickRequest = false;
        bool m_hasViewportRect = false;
    };
}
