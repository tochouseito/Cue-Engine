#pragma once

/// **********************************************************************
/// Editor 全体の UI 状態と各 View 更新を管理する
/// **********************************************************************

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>

// === C++ includes ===
#include <memory>

namespace Cue
{
    class Engine;
}

namespace Cue::RHI::DX12
{
    class D3D12Backend;
}

namespace Cue::Editor
{
    class DebugCamera;
    class DebugView;
    class Dockspace;
    class GameView;

    struct EditorManagerSetupInfo final
    {
        RHI::DX12::D3D12Backend* backend = nullptr; // Editor View が参照する描画 backend
        Engine* engine = nullptr;                   // GameWorld など Editor が参照する Runtime
        DebugCamera* debugCamera = nullptr;         // DebugView の入力で更新する Editor camera
    };

    /// @brief CueEngine の EditorManager と同じく、Editor の共有状態と UI 更新順を集約する。
    class EditorManager final
    {
    public:
        EditorManager();
        EditorManager(const EditorManager&) = delete;
        EditorManager& operator=(const EditorManager&) = delete;
        EditorManager(EditorManager&&) = delete;
        EditorManager& operator=(EditorManager&&) = delete;
        ~EditorManager();

        /// @brief Editor の依存と最小 View 群を初期化する。
        void initialize(const EditorManagerSetupInfo& a_info);

        /// @brief 1 frame 分の Editor UI を描画し、DebugCamera 入力を反映する。
        void update();

        /// @brief Hierarchy / Inspector が共有する選択中 Entity を返す。
        [[nodiscard]] GameCore::EntityId selected_entity_id() const noexcept
        {
            return m_selectedEntityId;
        }

        /// @brief Hierarchy / Inspector が共有する選択中 Scene を返す。
        [[nodiscard]] GameCore::SceneId selected_scene_id() const noexcept
        {
            return m_selectedSceneId;
        }

        /// @brief 選択中 Entity を更新する。
        void set_selected_entity_id(GameCore::EntityId a_entityId) noexcept
        {
            m_selectedEntityId = a_entityId;
        }

        /// @brief 選択中 Scene を更新する。
        void set_selected_scene_id(GameCore::SceneId a_sceneId) noexcept
        {
            m_selectedSceneId = a_sceneId;
        }

    private:
        RHI::DX12::D3D12Backend* m_backend = nullptr; // View 生成時と SRV 解決に使う非所有 backend
        Engine* m_engine = nullptr;                   // 今後 Hierarchy / Inspector が参照する非所有 Engine
        DebugCamera* m_debugCamera = nullptr;         // Engine が参照している DebugCamera

        std::unique_ptr<Dockspace> m_dockspace = nullptr;
        std::unique_ptr<GameView> m_gameView = nullptr;
        std::unique_ptr<DebugView> m_debugView = nullptr;

        GameCore::EntityId m_selectedEntityId = GameCore::k_invalidEntityId;
        GameCore::SceneId m_selectedSceneId = GameCore::k_invalidSceneId;
    };
} // namespace Cue::Editor
