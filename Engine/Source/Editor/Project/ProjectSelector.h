#pragma once

/// ************************************************************************************
/// cueproject.json を持つ Project を選択する Editor UI
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Runtime includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <array>
#include <string>
#include <string_view>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::PAL
{
    class IDialogService;
}

namespace Cue::Editor
{
    /// @brief Project 読み込み前に cueproject.json を持つフォルダを選択する。
    class ProjectSelector final
    {
    public:
        ProjectSelector(PAL::IDialogService& a_dialogService, Core::IO::IFileSystem& a_fileSystem) noexcept;
        ~ProjectSelector() = default;

        /// @brief 実行ファイルの配置場所から上位へ探索し、Project 選択 UI を開く。
        void open_from_executable_directory();

        /// @brief 現在の探索結果を維持したまま Project 選択 UI を開く。
        void open() noexcept;

        /// @brief Project 選択 UI を描画する。
        void update();

        /// @brief 読み込み要求された Project root を取り出す。
        bool consume_selected_project(Core::IO::Path& a_outRoot) noexcept;

        /// @brief Project 読み込みで発生したエラーを UI に表示する。
        void show_error(std::string_view a_message);

        [[nodiscard]] bool is_open() const noexcept
        {
            return m_isOpen;
        }

    private:
        void open_project_folder_dialog();
        void begin_create_project();
        void draw_create_project_dialog();
        void select_project_parent_directory();
        [[nodiscard]] Result create_project();
        bool validate_project_directory(const Core::IO::Path& a_projectPath);

        Core::IO::Path m_initialDirectory{};
        Core::IO::Path m_selectedRoot{};
        Core::IO::Path m_createParentDirectory{};
        std::string m_errorMessage{};
        std::array<char, 128> m_projectName{};
        PAL::IDialogService* m_dialogService = nullptr; // OS 標準ダイアログを表示する非所有サービス
        Core::IO::IFileSystem* m_fileSystem = nullptr; // Project フォルダ検証に使う非所有 FileSystem
        bool m_isOpen = false;
        bool m_isCreateProjectDialogOpen = false;
        bool m_hasSelectedProject = false;
    };
} // namespace Cue::Editor
