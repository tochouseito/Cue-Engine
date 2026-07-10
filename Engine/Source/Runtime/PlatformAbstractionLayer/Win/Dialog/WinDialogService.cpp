#include "WinDialogService.h"

// === win_platform includes ===
#include "../ConvertHresult.h"
#include "../ConvertUTF.h"

// === Windows API includes ===
#include <shobjidl.h>

namespace
{
    using Cue::Result;
    using Cue::Severity;

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

    [[nodiscard]] Result set_title(IFileDialog& a_dialog, const char* a_title)
    {
        if (a_title == nullptr || a_title[0] == '\0')
        {
            return Result::ok();
        }

        std::wstring title{};
        Result result = Cue::PAL::Win::utf8_to_wide(a_title, &title);
        if (!result)
        {
            return result;
        }

        const HRESULT hresult = a_dialog.SetTitle(title.c_str());
        if (FAILED(hresult))
        {
            return Result::fail(
                Cue::PAL::Win::convert_hresult_code(hresult),
                Severity::Error,
                "Failed to set dialog title.");
        }

        return Result::ok();
    }

    [[nodiscard]] Result set_initial_directory(IFileDialog& a_dialog, const Cue::Core::IO::Path& a_path)
    {
        if (a_path.is_empty())
        {
            return Result::ok();
        }

        std::wstring initialDirectory{};
        Result result = Cue::PAL::Win::utf8_to_wide(a_path.normalize().utf8(), &initialDirectory);
        if (!result)
        {
            return result;
        }
        to_native_separator(initialDirectory);

        Microsoft::WRL::ComPtr<IShellItem> folder = nullptr;
        HRESULT hresult = ::SHCreateItemFromParsingName(
            initialDirectory.c_str(),
            nullptr,
            IID_PPV_ARGS(&folder));
        if (FAILED(hresult))
        {
            return Result::fail(
                Cue::PAL::Win::convert_hresult_code(hresult),
                Severity::Error,
                "Failed to create initial folder shell item.");
        }

        hresult = a_dialog.SetDefaultFolder(folder.Get());
        if (FAILED(hresult))
        {
            return Result::fail(
                Cue::PAL::Win::convert_hresult_code(hresult),
                Severity::Error,
                "Failed to set default folder.");
        }

        hresult = a_dialog.SetFolder(folder.Get());
        if (FAILED(hresult))
        {
            return Result::fail(
                Cue::PAL::Win::convert_hresult_code(hresult),
                Severity::Error,
                "Failed to set initial folder.");
        }

        return Result::ok();
    }

    [[nodiscard]] Result get_selected_path(
        IFileDialog& a_dialog,
        Cue::Core::IO::Path& a_outPath,
        bool& a_outSelected)
    {
        a_outPath = {};
        a_outSelected = false;

        Microsoft::WRL::ComPtr<IShellItem> selectedItem = nullptr;
        HRESULT hresult = a_dialog.GetResult(&selectedItem);
        if (FAILED(hresult))
        {
            return Result::fail(
                Cue::PAL::Win::convert_hresult_code(hresult),
                Severity::Error,
                "Failed to get selected dialog item.");
        }

        PWSTR selectedPath = nullptr;
        hresult = selectedItem->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath);
        if (FAILED(hresult))
        {
            return Result::fail(
                Cue::PAL::Win::convert_hresult_code(hresult),
                Severity::Error,
                "Failed to get selected dialog path.");
        }

        std::string utf8Path{};
        Result result = Cue::PAL::Win::wide_to_utf8(selectedPath, &utf8Path);
        ::CoTaskMemFree(selectedPath);
        if (!result)
        {
            return result;
        }

        a_outPath = Cue::Core::IO::Path(utf8Path).normalize();
        a_outSelected = true;
        return Result::ok();
    }
}

namespace Cue::PAL::Win
{
    WinDialogService::WinDialogService(HWND a_ownerWindow) noexcept
        : m_ownerWindow(a_ownerWindow)
    {
    }

    Result WinDialogService::open_folder_dialog(
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

        Result result = set_title(*dialog.Get(), a_desc.title);
        if (!result)
        {
            return result;
        }
        result = set_initial_directory(*dialog.Get(), a_desc.initialDirectory);
        if (!result)
        {
            return result;
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

        return get_selected_path(*dialog.Get(), a_outPath, a_outSelected);
    }

    Result WinDialogService::save_file_dialog(
        const SaveFileDialogDesc& a_desc,
        Core::IO::Path& a_outPath,
        bool& a_outSelected)
    {
        a_outPath = {};
        a_outSelected = false;

        Microsoft::WRL::ComPtr<IFileSaveDialog> dialog = nullptr;
        HRESULT hresult = ::CoCreateInstance(
            CLSID_FileSaveDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to create save file dialog.");
        }

        DWORD options = 0;
        hresult = dialog->GetOptions(&options);
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to get save file dialog options.");
        }

        hresult = dialog->SetOptions(
            options |
            FOS_FORCEFILESYSTEM |
            FOS_PATHMUSTEXIST |
            FOS_NOCHANGEDIR |
            FOS_OVERWRITEPROMPT);
        if (FAILED(hresult))
        {
            return Result::fail(
                convert_hresult_code(hresult),
                Severity::Error,
                "Failed to set save file dialog options.");
        }

        Result result = set_title(*dialog.Get(), a_desc.title);
        if (!result)
        {
            return result;
        }
        result = set_initial_directory(*dialog.Get(), a_desc.initialDirectory);
        if (!result)
        {
            return result;
        }

        if (a_desc.defaultFileName != nullptr && a_desc.defaultFileName[0] != '\0')
        {
            std::wstring fileName{};
            result = utf8_to_wide(a_desc.defaultFileName, &fileName);
            if (!result)
            {
                return result;
            }

            hresult = dialog->SetFileName(fileName.c_str());
            if (FAILED(hresult))
            {
                return Result::fail(
                    convert_hresult_code(hresult),
                    Severity::Error,
                    "Failed to set save file name.");
            }
        }

        if (a_desc.defaultExtension != nullptr && a_desc.defaultExtension[0] != '\0')
        {
            std::wstring extension{};
            result = utf8_to_wide(a_desc.defaultExtension, &extension);
            if (!result)
            {
                return result;
            }

            hresult = dialog->SetDefaultExtension(extension.c_str());
            if (FAILED(hresult))
            {
                return Result::fail(
                    convert_hresult_code(hresult),
                    Severity::Error,
                    "Failed to set save file extension.");
            }
        }

        if (a_desc.filterName != nullptr && a_desc.filterPattern != nullptr)
        {
            std::wstring filterName{};
            std::wstring filterPattern{};
            result = utf8_to_wide(a_desc.filterName, &filterName);
            if (!result)
            {
                return result;
            }
            result = utf8_to_wide(a_desc.filterPattern, &filterPattern);
            if (!result)
            {
                return result;
            }

            const COMDLG_FILTERSPEC filter = {filterName.c_str(), filterPattern.c_str()};
            hresult = dialog->SetFileTypes(1, &filter);
            if (FAILED(hresult))
            {
                return Result::fail(
                    convert_hresult_code(hresult),
                    Severity::Error,
                    "Failed to set save file filter.");
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
                "Failed to show save file dialog.");
        }

        return get_selected_path(*dialog.Get(), a_outPath, a_outSelected);
    }
}
