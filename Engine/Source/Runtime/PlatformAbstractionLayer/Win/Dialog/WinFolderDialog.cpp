#include "WinFolderDialog.h"

/// === win_platform includes ===
#include "../ConvertHresult.h"
#include "../ConvertUTF.h"

/// === Windows API includes ===
#include <shobjidl.h>

namespace
{
    void to_native_separator(std::wstring& a_path) noexcept
    {
        for (wchar_t& character : a_path)
        {
            if (character == L'/')
            {
                character = L'\\';
            }
        }
    }
}

namespace Cue::PAL::Win
{
    WinFolderDialog::WinFolderDialog(HWND a_ownerWindow) noexcept
        : m_ownerWindow(a_ownerWindow)
    {
    }

    Result WinFolderDialog::open_folder_dialog(
        const FolderDialogDesc& a_desc,
        Core::IO::Path& a_outPath,
        bool& a_outSelected)
    {
        a_outPath = {};
        a_outSelected = false;

        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog = nullptr;
        HRESULT hresult = ::CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to create folder dialog.");
        }

        DWORD options = 0;
        hresult = dialog->GetOptions(&options);
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to get folder dialog options.");
        }

        hresult = dialog->SetOptions(
            options |
            FOS_PICKFOLDERS |
            FOS_FORCEFILESYSTEM |
            FOS_PATHMUSTEXIST |
            FOS_NOCHANGEDIR);
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to set folder dialog options.");
        }

        if (a_desc.title != nullptr && a_desc.title[0] != '\0')
        {
            std::wstring title{};
            Result titleResult = utf8_to_wide(a_desc.title, &title);
            if (!titleResult)
            {
                return titleResult;
            }

            hresult = dialog->SetTitle(title.c_str());
            if (FAILED(hresult))
            {
                return Result::fail(
                    convert_hresult_code(hresult),
                    Severity::Error,
                    "Failed to set folder dialog title.");
            }
        }

        if (!a_desc.initialDirectory.is_empty())
        {
            std::wstring initialDirectory{};
            Result pathResult = utf8_to_wide(a_desc.initialDirectory.normalize().utf8(), &initialDirectory);
            if (!pathResult)
            {
                return pathResult;
            }
            to_native_separator(initialDirectory);

            Microsoft::WRL::ComPtr<IShellItem> folder = nullptr;
            hresult = ::SHCreateItemFromParsingName(
                initialDirectory.c_str(),
                nullptr,
                IID_PPV_ARGS(&folder));
            if (FAILED(hresult))
            {
                return Result::fail(
                    convert_hresult_code(hresult),
                    Severity::Error,
                    "Failed to create initial folder shell item.");
            }

            // 既定フォルダと現在フォルダの両方を指定し、Shell 履歴より Project 候補を優先する
            hresult = dialog->SetDefaultFolder(folder.Get());
            if (FAILED(hresult))
            {
                return Result::fail(
                    convert_hresult_code(hresult),
                    Severity::Error,
                    "Failed to set default folder.");
            }

            hresult = dialog->SetFolder(folder.Get());
            if (FAILED(hresult))
            {
                return Result::fail(
                    convert_hresult_code(hresult),
                    Severity::Error,
                    "Failed to set initial folder.");
            }
        }

        hresult = dialog->Show(m_ownerWindow);
        if (hresult == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            return Result::ok();
        }
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to show folder dialog.");
        }

        Microsoft::WRL::ComPtr<IShellItem> selectedItem = nullptr;
        hresult = dialog->GetResult(&selectedItem);
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to get selected folder.");
        }

        PWSTR selectedPath = nullptr;
        hresult = selectedItem->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath);
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to get selected folder path.");
        }

        std::string utf8Path{};
        Result pathResult = wide_to_utf8(selectedPath, &utf8Path);
        ::CoTaskMemFree(selectedPath);
        if (!pathResult)
        {
            return pathResult;
        }

        a_outPath = Core::IO::Path(utf8Path).normalize();
        a_outSelected = true;
        return Result::ok();
    }
}
