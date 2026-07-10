#pragma once

/// ********************************************************************************
/// OS 標準ダイアログを扱う PAL サービス
/// ********************************************************************************

/// === Base includes ===
#include <CueResult.h>

/// === Core includes ===
#include <IO/Path.h>

namespace Cue::PAL
{
    /// @brief OS 標準フォルダ選択ダイアログの表示設定
    struct FolderDialogDesc final
    {
        const char* title = nullptr; // ダイアログタイトル
        Core::IO::Path initialDirectory{}; // 初期表示フォルダ
    };

    /// @brief OS 標準保存ファイルダイアログの表示設定
    struct SaveFileDialogDesc final
    {
        const char* title = nullptr; // ダイアログタイトル
        const char* defaultFileName = nullptr; // 初期ファイル名
        const char* defaultExtension = nullptr; // 拡張子未入力時に補う拡張子
        const char* filterName = nullptr; // 表示するファイル種別名
        const char* filterPattern = nullptr; // 表示するファイル種別パターン
        Core::IO::Path initialDirectory{}; // 初期表示フォルダ
    };

    /// @brief OS 標準ダイアログを Runtime から独立して扱うサービス
    class IDialogService
    {
    public:
        virtual ~IDialogService() = default;

        /// @brief OS 標準フォルダ選択ダイアログを表示する
        virtual Result open_folder_dialog(
            const FolderDialogDesc& a_desc,
            Core::IO::Path& a_outPath,
            bool& a_outSelected) = 0;

        /// @brief OS 標準保存ファイルダイアログを表示する
        virtual Result save_file_dialog(
            const SaveFileDialogDesc& a_desc,
            Core::IO::Path& a_outPath,
            bool& a_outSelected) = 0;
    };
}
