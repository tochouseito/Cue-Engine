#include "DebugView.h"

namespace Cue::Editor
{
    void DebugView::update()
    {
        m_isViewportHovered = false;
        m_isFocused = false;

        if (m_backend == nullptr || m_backend->get_view_manager() == nullptr)
        {
            CUE_ASSERT_MSG(false, "DebugView: Backend is null");
            return;
        }

        // resize 後は FinalColorSRV が再作成されるため、handle を毎フレーム解決する
        Result viewResult =
            m_backend->get_view_manager()->get_view("FinalColorSRV",
                m_finalColorSrvHandle);
        CUE_ASSERT_FORMAT(success(viewResult), "Failed to get FinalColorSRV: {}", viewResult.message.data());

        // ImGui ウィンドウの描画
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        const bool isVisible =
            ImGui::Begin("DebugView", nullptr, windowFlags);
        m_isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        // ウィンドウが閉じられた場合は描画をスキップ
        if (!isVisible)
        {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        // メニューバーの描画
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Test"))
            {
                ImGui::MenuItem("DebugView Test", nullptr, false, false);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // メニュー領域を除いた残りを表示先として使う
        const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
        m_viewportWidth = static_cast<uint32_t>(std::max(availableRegion.x, 1.0f));
        m_viewportHeight = static_cast<uint32_t>(std::max(availableRegion.y, 1.0f));
        D3D12_GPU_DESCRIPTOR_HANDLE finalColorSrvGpuDescHandle =
            m_backend->get_gpu_descriptor_handle(
                m_finalColorSrvHandle,
                m_backend->current_back_buffer_index(),
                m_backend->buffer_count());

        // backend のサイズは FrameController のリサイズセーフポイント適用後に更新される
        // FinalColor はこのサイズで再作成されるため、pick 用 pixel 座標の基準にする
        const uint32_t finalColorWidth = m_backend->width();
        const uint32_t finalColorHeight = m_backend->height();

        if (finalColorSrvGpuDescHandle.ptr != 0 &&
            finalColorWidth > 0 && finalColorHeight > 0 &&
            availableRegion.x > 0.0f && availableRegion.y > 0.0f)
        {
            if (availableRegion.x <= 0.0f || availableRegion.y <= 0.0f)
            {
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }

            ImGui::Image(
                static_cast<ImTextureID>(finalColorSrvGpuDescHandle.ptr),
                availableRegion);
            m_isViewportHovered = ImGui::IsItemHovered();

            // ImGui 上の表示矩形は dock 配置で変わる
            // overlay と pick は描画結果から毎フレーム取得する
            const bool isImageClicked =
                ImGui::IsItemClicked(ImGuiMouseButton_Left);
            bool isOverlayBlockingPick = false;
            if (!isOverlayBlockingPick && isImageClicked)
            {
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}
