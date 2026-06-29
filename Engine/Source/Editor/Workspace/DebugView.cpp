#include "DebugView.h"

namespace Cue::Editor
{
    void DebugView::update()
    {
        if (m_backend == nullptr || m_backend->get_view_manager() == nullptr)
        {
            CUE_ASSERT_MSG(false, "DebugView: Backend is null");
            return;
        }

        // resize 後は DebugColorSRV が再作成されるため、handle を毎フレーム解決する
        Result viewResult =
            m_backend->get_view_manager()->get_view("DebugColorSRV",
                m_debugColorSrvHandle);
        CUE_ASSERT_FORMAT(success(viewResult), "Failed to get DebugColorSRV: {}", viewResult.message.data());

        // ImGui ウィンドウの描画
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        const bool isVisible =
            ImGui::Begin("DebugView", nullptr, windowFlags);

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

        // メニュー領域を除いた残りを DebugColor の表示先として使う
        const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
        D3D12_GPU_DESCRIPTOR_HANDLE debugColorSrvGpuDescHandle =
            m_backend->get_gpu_descriptor_handle(
                m_debugColorSrvHandle,
                m_backend->current_back_buffer_index(),
                m_backend->buffer_count());

        // backend のサイズは FrameController のリサイズセーフポイント適用後に更新される
        // DebugColor はこのサイズで再作成されるため、pick 用 pixel 座標の基準にする
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

            ImGui::Image(
                static_cast<ImTextureID>(debugColorSrvGpuDescHandle.ptr),
                availableRegion);

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
