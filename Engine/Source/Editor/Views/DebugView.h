// DebugView の役割と公開要素を定義する

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
        // EditorManager 側のメニュー構成を DebugView へ差し込むための callback
        using DrawAddMenuCallback = void (*)(void* a_context);

        // overlay が入力を消費したかを返し、pick と gizmo 操作の競合を避ける
        using DrawOverlayCallback = bool (*)(
            void* a_context,
            const ImVec2& a_viewportMin,
            const ImVec2& a_viewportMax,
            ImDrawList* a_drawList);

        // Scene/View メニューは DebugView 側で所有せず、呼び出し元の状態に委譲する
        using DrawSceneMenuCallback = void (*)(void* a_context);
        using DrawViewMenuCallback = void (*)(void* a_context);

        /// @brief DebugColor 上の pick 位置を通知する要求データ
        struct PickRequest final
        {
            // 表示矩形内の正規化座標。ImGui の表示サイズが描画解像度と異なるため保持する
            float normalizedX = 0.0f;
            float normalizedY = 0.0f;

            // DebugColor の実ピクセル座標。GPU readback は描画先 texture のサイズで行う
            uint32_t pixelX = 0;
            uint32_t pixelY = 0;
        };

        /// @brief DebugView を構築する
        /// @param a_backend DebugColor SRV と描画サイズを取得する backend
        /// @param a_camera DebugView 用 camera。nullptr の場合は camera 更新を行わない
        DebugView(
            RHI::DX12::D3D12Backend* a_backend,
            DebugCamera* a_camera)
            : m_backend(a_backend)
            , m_camera(a_camera)
        {
        }
        ~DebugView() = default;

        /// @brief 追加メニューの描画 callback を設定する
        void set_add_menu_callback(
            void* a_context,
            DrawAddMenuCallback a_callback) noexcept
        {
            m_addMenuContext = a_context;
            m_drawAddMenuCallback = a_callback;
        }

        /// @brief DebugView 上の overlay 描画 callback を設定する
        void set_overlay_callback(
            void* a_context,
            DrawOverlayCallback a_callback) noexcept
        {
            m_overlayContext = a_context;
            m_drawOverlayCallback = a_callback;
        }

        /// @brief ビューメニューの描画 callback を設定する
        void set_view_menu_callback(
            void* a_context,
            DrawViewMenuCallback a_callback) noexcept
        {
            m_viewMenuContext = a_context;
            m_drawViewMenuCallback = a_callback;
        }

        /// @brief シーンメニューの描画 callback を設定する
        void set_scene_menu_callback(
            void* a_context,
            DrawSceneMenuCallback a_callback) noexcept
        {
            m_sceneMenuContext = a_context;
            m_drawSceneMenuCallback = a_callback;
        }

        /// @brief 未処理の pick 要求を取り出す
        /// @return 要求を取り出した場合は true
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

        /// @brief 保留中の pick 要求を破棄する
        void clear_pick_request() noexcept
        {
            m_hasPickRequest = false;
        }

        /// @brief 最新フレームで描画した DebugView の ImGui 表示矩形を取得する
        /// @return 表示矩形が有効な場合は true
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

        /// @brief DebugView の ImGui ウィンドウを更新する
        void update()
        {
            // 非表示や backend 未初期化のフレームでは前フレームの矩形を使わせない
            m_hasViewportRect = false;
            if (m_backend == nullptr || m_backend->get_view_manager() == nullptr)
            {
                return;
            }

            // resize 後は DebugColorSRV が再作成されるため、handle を毎フレーム解決する
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

                if (m_camera != nullptr)
                {
                    // camera は実ウィンドウではなく、DebugView の表示領域に合わせる
                    m_camera->set_aspect(availableRegion.x / availableRegion.y);
                    m_camera->update(ImGui::IsWindowHovered());
                }

                ImGui::Image(
                    static_cast<ImTextureID>(debugColorSrvGpuDescHandle.ptr),
                    availableRegion);

                // ImGui 上の表示矩形は dock 配置で変わる
                // overlay と pick は描画結果から毎フレーム取得する
                m_viewportMin = ImGui::GetItemRectMin();
                m_viewportMax = ImGui::GetItemRectMax();
                m_hasViewportRect = true;
                const bool isImageClicked =
                    ImGui::IsItemClicked(ImGuiMouseButton_Left);
                bool isOverlayBlockingPick = false;
                if (m_drawOverlayCallback != nullptr)
                {
                    // gizmo などがクリックを使った場合は object pick を発行しない
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
                        // ImGui の表示座標を DebugColor texture の座標へ変換する
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
        // DebugColor と descriptor を取得する非所有 backend
        RHI::DX12::D3D12Backend* m_backend = nullptr;

        // DebugView の表示領域に追従する非所有 camera
        DebugCamera* m_camera = nullptr;

        // resize 後の再作成に追従するため update() で再解決する
        RHI::ViewHandle m_debugColorSrvHandle{};

        // Engine 側の readback 処理へ 1 回だけ渡す pick 要求
        PickRequest m_pickRequest{};

        // overlay と外部 hit test が参照する最新の ImGui 表示矩形
        ImVec2 m_viewportMin = ImVec2(0.0f, 0.0f);
        ImVec2 m_viewportMax = ImVec2(0.0f, 0.0f);

        // callback の所有権は呼び出し元に残すため、context は非所有で保持する
        void* m_addMenuContext = nullptr;
        void* m_overlayContext = nullptr;
        void* m_sceneMenuContext = nullptr;
        void* m_viewMenuContext = nullptr;

        // EditorManager 側の状態を DebugView に直接持ち込まないための描画 callback
        DrawAddMenuCallback m_drawAddMenuCallback = nullptr;
        DrawOverlayCallback m_drawOverlayCallback = nullptr;
        DrawSceneMenuCallback m_drawSceneMenuCallback = nullptr;
        DrawViewMenuCallback m_drawViewMenuCallback = nullptr;

        // pick / 表示矩形はフレームごとの有効性が異なるため明示フラグで管理する
        bool m_hasPickRequest = false;
        bool m_hasViewportRect = false;
    };
}
