#pragma once

/// **********************************************************************
/// Editor 全体の UI 状態と各 View 更新を管理する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Runtime includes ===
#include <GameCore/GameCoreTypes.h>
#include <IO/Path.h>

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
    class GameView;
    class Hierarchy;
    class Inspector;
    class ProjectSelector;

    struct EditorManagerSetupInfo final
    {
        RHI::DX12::D3D12Backend* backend = nullptr; // Editor View が参照する描画 backend
        Engine* engine = nullptr;                   // GameWorld など Editor が参照する Runtime
        DebugCamera* debugCamera = nullptr;         // DebugView の入力で更新する Editor camera
        PAL::IDialogService* dialogService = nullptr; // OS 標準 UI を呼び出す非所有サービス
        Core::IO::IFileSystem* fileSystem = nullptr; // Project 設定を読み込む非所有 FileSystem
        Core::CQRS::Bridge* gameCommandBridge = nullptr; // GameWorld 編集コマンドの送信先
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

        /// @brief Project 設定を読み込み、Engine から Assets フォルダを参照できるようにする。
        [[nodiscard]] Result load_project(const Core::IO::Path& a_root);

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
        void draw_dockspace();
        void draw_menu_bar();
        void draw_file_menu_items();
        void draw_add_menu_items();
        void draw_view_menu_items();
        void open_project_selector();
        void update_project_selector();
        void submit_empty_object_command();
        void show_and_focus_window(const char* a_windowName);
        bool prepare_window_focus(const char* a_windowName);
        void focus_pending_window();

        RHI::DX12::D3D12Backend* m_backend = nullptr; // View 生成時と SRV 解決に使う非所有 backend
        Engine* m_engine = nullptr;                   // 今後 Hierarchy / Inspector が参照する非所有 Engine
        DebugCamera* m_debugCamera = nullptr;         // Engine が参照している DebugCamera
        Core::IO::IFileSystem* m_fileSystem = nullptr; // Project 設定ファイルの読み込み元
        Core::CQRS::Bridge* m_gameCommandBridge = nullptr; // Editor から GameWorld を編集する command bridge

        std::unique_ptr<GameView> m_gameView = nullptr;
        std::unique_ptr<DebugView> m_debugView = nullptr;
        std::unique_ptr<Hierarchy> m_hierarchy = nullptr;
        std::unique_ptr<Inspector> m_inspector = nullptr;
        std::unique_ptr<ProjectSelector> m_projectSelector = nullptr;

        GameCore::EntityId m_selectedEntityId = GameCore::k_invalidEntityId;
        GameCore::SceneId m_selectedSceneId = GameCore::k_invalidSceneId;
        Core::IO::Path m_projectRootPath{};
        Core::IO::Path m_assetRootPath{};
        std::string m_projectName{};
        std::string m_startupScene{};
        std::string m_pendingFocusWindowName{}; // View メニューから要求された focus 先 window
    };
} // namespace Cue::Editor
