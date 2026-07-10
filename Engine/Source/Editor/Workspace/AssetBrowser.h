#pragma once

/// ************************************************************************************
/// Project の Asset を表示し、Scene 操作要求を発行する Workspace
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Editor includes ===
#include "AssetSelection.h"

// === Runtime includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    /// @brief Project asset をツリー表示する Editor Workspace
    class AssetBrowser final
    {
      public:
        /// @brief Asset 操作要求を発行する Browser を構築する
        /// @param a_fileSystem Asset tree を取得する FileSystem
        /// @param a_selectedAsset Inspector と共有する選択状態
        AssetBrowser(Core::IO::IFileSystem& a_fileSystem,
                     AssetSelection* a_selectedAsset) noexcept;

        /// @brief 表示対象の Asset root を設定する
        void set_asset_root_path(const Core::IO::Path& a_path);

        /// @brief 開いている Scene を選択状態へ反映する
        void set_current_scene_path(const Core::IO::Path& a_path) noexcept;

        /// @brief Asset ツリーを再取得する
        [[nodiscard]] Result refresh();

        /// @brief Asset Browser を描画する
        void update();

        /// @brief ダブルクリックされた Scene path を取り出す
        bool consume_open_scene_request(Core::IO::Path& a_outPath) noexcept;

        /// @brief 新規 Scene を作成する親フォルダを取り出す
        bool consume_new_scene_request(Core::IO::Path& a_outDirectory) noexcept;

        /// @brief 単一選択された Asset の情報を取り出す
        bool consume_asset_selection(AssetSelection& a_outSelection) noexcept;

      private:
        struct Entry final
        {
            Core::IO::Path path{};
            std::string name{};
            std::vector<Entry> children{};
            uint64_t sizeBytes = 0;
            AssetKind kind = AssetKind::unknown;
            bool isDirectory = false;
        };

        [[nodiscard]] Result collect_entries(const Core::IO::Path& a_directory,
                                             std::vector<Entry>& a_outEntries);
        void draw_entries(const std::vector<Entry>& a_entries);
        void draw_entry(const Entry& a_entry);
        [[nodiscard]] static bool is_scene_file(AssetKind a_kind) noexcept;

        Core::IO::Path m_assetRootPath{};
        Core::IO::Path m_currentScenePath{};
        Core::IO::Path m_pendingOpenScenePath{};
        Core::IO::Path m_pendingNewSceneDirectory{};
        AssetSelection m_pendingAssetSelection{};
        std::string m_errorMessage{};
        std::vector<Entry> m_entries{};
        Core::IO::IFileSystem* m_fileSystem =
            nullptr; // Asset tree を走査する非所有 FileSystem
        AssetSelection* m_selectedAsset =
            nullptr; // Entity 選択と分離された Editor 共有の Asset 選択状態
        bool m_hasOpenSceneRequest = false;
        bool m_hasNewSceneRequest = false;
        bool m_hasAssetSelectionRequest = false;
    };
} // namespace Cue::Editor
