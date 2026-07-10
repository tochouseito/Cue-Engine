#pragma once

/// ********************************************************************************
/// Windows 標準ダイアログサービス
/// ********************************************************************************

// === PAL includes ===
#include <Dialog/DialogService.h>

// === win_platform includes ===
#include "../WinCommon.h"

namespace Cue::PAL::Win
{
    /// @brief Windows Shell のファイル・フォルダダイアログ実装
    class WinDialogService final : public IDialogService
    {
    public:
        explicit WinDialogService(HWND a_ownerWindow) noexcept;
        ~WinDialogService() override = default;

        /// @brief Windows 標準フォルダ選択ダイアログを表示する
        Result open_folder_dialog(
            const FolderDialogDesc& a_desc,
            Core::IO::Path& a_outPath,
            bool& a_outSelected) override;

        /// @brief Windows 標準保存ファイルダイアログを表示する
        Result save_file_dialog(
            const SaveFileDialogDesc& a_desc,
            Core::IO::Path& a_outPath,
            bool& a_outSelected) override;

    private:
        HWND m_ownerWindow = nullptr; // ダイアログの親にする非所有ウィンドウ
    };
}
