#pragma once

/// ********************************************************************************
/// Windows 標準フォルダ選択ダイアログ
/// ********************************************************************************

/// === PAL includes ===
#include <Dialog/DialogService.h>

/// === win_platform includes ===
#include "../WinCommon.h"

namespace Cue::PAL::Win
{
    /// @brief Windows Shell のフォルダ選択ダイアログ実装
    class WinFolderDialog final : public IDialogService
    {
    public:
        explicit WinFolderDialog(HWND a_ownerWindow) noexcept;
        ~WinFolderDialog() override = default;

        /// @brief Windows 標準フォルダ選択ダイアログを表示する
        Result open_folder_dialog(
            const FolderDialogDesc& a_desc,
            Core::IO::Path& a_outPath,
            bool& a_outSelected) override;

    private:
        HWND m_ownerWindow = nullptr; // ダイアログの親にする非所有ウィンドウ
    };
}
