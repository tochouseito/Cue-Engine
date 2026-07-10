#pragma once

/// **********************************************************************
/// Editor 全体の UI 状態と各 View 更新を管理する
/// **********************************************************************

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>
#include <IO/Path.h>

// === Base includes ===
#include <CueResult.h>

// === Editor includes ===
#include "Workspace/AssetSelection.h"

// === C++ includes ===
#include <memory>
#include <string>

namespace Cue
{
    class Engine;
}

namespace Cue::Core::CQRS
{
    class Bridge;
}

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::PAL
{
    class IDialogService;
}

namespace Cue::RHI::DX12
{
    class D3D12Backend;
}

namespace Cue::Editor
{
    class DebugCamera;
    class DebugView;
    class AssetBrowser;
    class EditorSceneManager;
    class GameView;
    class Hierarchy;
    class Inspector;
    class EditorProject;
    class ProjectSelector;

    struct EditorManagerSetupInfo final
    {
        RHI::DX12::D3D12Backend* backend =
            nullptr;              // Editor View が参照する描画 backend
        Engine* engine = nullptr; // GameWorld など Editor が参照する Runtime
        DebugCamera* debugCamera =
            nullptr; // DebugView の入力で更新する Editor camera
        PAL::IDialogService* dialogService =
            nullptr; // OS 標準 UI を呼び出す非所有サービス
        Core::IO::IFileSystem* fileSystem =
            nullptr; // Project 設定を読み込む非所有 FileSystem
        Core::CQRS::Bridge* gameCommandBridge =
            nullptr; // GameWorld 編集コマンドの送信先
    };

    /// @brief CueEngine の EditorManager と同じく、Editor の共有状態と UI
    /// 更新順を集約する。
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

        /// @brief 未保存 Scene がある場合は終了確認を要求する
        [[nodiscard]] bool request_exit();

        /// @brief 保存確認から確定した終了要求を取り出す
        [[nodiscard]] bool consume_exit_request() noexcept;

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
        enum class SceneTransition : uint8_t
        {
            none = 0,
            newScene,
            openScene,
            openProject,
            exit,
        };

        void draw_dockspace();
        void draw_menu_bar();
        void draw_file_menu_items();
        void draw_add_menu_items();
        void draw_view_menu_items();
        void open_project_selector();
        void update_project_selector();
        void draw_scene_transition_dialog();
        void request_scene_transition(SceneTransition a_transition,
                                      const Core::IO::Path& a_projectRoot = {});
        void request_new_scene(const Core::IO::Path& a_directory);
        void request_open_scene(const Core::IO::Path& a_path);
        void select_asset(const AssetSelection& a_selection) noexcept;
        void apply_scene_transition();
        void load_project(const Core::IO::Path& a_projectRoot);
        [[nodiscard]] Result save_current_scene(bool& a_outSaved);
        [[nodiscard]] Result save_scene_as(bool a_setsStartupScene, bool& a_outSaved);
        void show_scene_error(const Result& a_result);
        void clear_selection() noexcept;
        void submit_empty_object_command();
        void show_and_focus_window(const char* a_windowName);
        bool prepare_window_focus(const char* a_windowName);
        void focus_pending_window();

        RHI::DX12::D3D12Backend* m_backend =
            nullptr; // View 生成時と SRV 解決に使う非所有 backend
        Engine* m_engine =
            nullptr;                          // 今後 Hierarchy / Inspector が参照する非所有 Engine
        DebugCamera* m_debugCamera = nullptr; // Engine が参照している DebugCamera
        PAL::IDialogService* m_dialogService =
            nullptr; // Scene 保存先を選択する非所有 DialogService
        Core::CQRS::Bridge* m_gameCommandBridge =
            nullptr; // Editor から GameWorld を編集する command bridge

        std::unique_ptr<GameView> m_gameView = nullptr;
        std::unique_ptr<DebugView> m_debugView = nullptr;
        std::unique_ptr<AssetBrowser> m_assetBrowser = nullptr;
        std::unique_ptr<Hierarchy> m_hierarchy = nullptr;
        std::unique_ptr<Inspector> m_inspector = nullptr;
        std::unique_ptr<EditorProject> m_project = nullptr;
        std::unique_ptr<ProjectSelector> m_projectSelector = nullptr;
        std::unique_ptr<EditorSceneManager> m_sceneManager = nullptr;

        GameCore::EntityId m_selectedEntityId = GameCore::k_invalidEntityId;
        GameCore::SceneId m_selectedSceneId = GameCore::k_invalidSceneId;
        AssetSelection m_selectedAsset{};
        Core::IO::Path m_pendingProjectRoot{};
        Core::IO::Path m_pendingScenePath{};
        Core::IO::Path m_pendingSceneDirectory{};
        Core::IO::Path m_newSceneSaveDirectory{};
        std::string
            m_pendingFocusWindowName{}; // View メニューから要求された focus 先 window
        SceneTransition m_pendingSceneTransition = SceneTransition::none;
        bool m_shouldOpenSceneTransitionDialog = false;
        bool m_isExitRequested = false;
    };
} // namespace Cue::Editor
