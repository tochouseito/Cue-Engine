#include "Dialog/DirectoryDialog.h"

// === PAL includes ===
#include "ConvertUTF.h"

// === C++ includes ===
#include <thread>

// === Windows API includes ===
#include "stdafx.h"
#include <shobjidl.h>

namespace Cue::PAL::Win
{
    namespace
    {
        Result pick_directory_dialog_impl(
            std::string_view a_title,
            std::string_view a_initialDirectory,
            std::string* a_outSelectedDirectory,
            bool* a_outWasSelected
        ) noexcept
        {
            IFileDialog* fileDialog = nullptr;
            HRESULT hr = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fileDialog));
            if (FAILED(hr))
            {
                return Result::fail(
                    convert_hresult_code(hr), Severity::Error,
                    "Failed to initialize directory picker dialog.");
            }

            DWORD options = 0;
            hr = fileDialog->GetOptions(&options);
            if (SUCCEEDED(hr))
            {
                hr = fileDialog->SetOptions(
                    options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            }

            if (SUCCEEDED(hr) && !a_title.empty())
            {
                std::wstring wideTitle{};
                Result result = utf8_to_wide(a_title, &wideTitle);
                if (!result)
                {
                    fileDialog->Release();
                    return result;
                }

                hr = fileDialog->SetTitle(wideTitle.c_str());
            }

            if (SUCCEEDED(hr) && !a_initialDirectory.empty())
            {
                std::wstring wideDirectory{};
                Result result = utf8_to_wide(a_initialDirectory, &wideDirectory);
                if (!result)
                {
                    fileDialog->Release();
                    return result;
                }

                for (wchar_t& currentChar : wideDirectory)
                {
                    if (currentChar == L'/')
                    {
                        currentChar = L'\\';
                    }
                }

                IShellItem* defaultFolder = nullptr;
                const HRESULT folderHr = ::SHCreateItemFromParsingName(
                    wideDirectory.c_str(), nullptr, IID_PPV_ARGS(&defaultFolder));
                if (SUCCEEDED(folderHr))
                {
                    fileDialog->SetFolder(defaultFolder);
                    defaultFolder->Release();
                }
            }

            if (SUCCEEDED(hr))
            {
                hr = fileDialog->Show(nullptr);
            }

            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            {
                fileDialog->Release();
                return Result::ok();
            }

            if (FAILED(hr))
            {
                fileDialog->Release();
                return Result::fail(
                    convert_hresult_code(hr), Severity::Error,
                    "Failed to show directory picker dialog.");
            }

            IShellItem* shellItem = nullptr;
            hr = fileDialog->GetResult(&shellItem);
            if (FAILED(hr))
            {
                fileDialog->Release();
                return Result::fail(
                    convert_hresult_code(hr), Severity::Error,
                    "Failed to get selected directory.");
            }

            PWSTR selectedPath = nullptr;
            hr = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath);
            if (FAILED(hr) || selectedPath == nullptr)
            {
                shellItem->Release();
                fileDialog->Release();
                return Result::fail(
                    convert_hresult_code(hr), Severity::Error,
                    "Failed to resolve selected directory path.");
            }

            Result result = wide_to_utf8(selectedPath, a_outSelectedDirectory);
            ::CoTaskMemFree(selectedPath);
            shellItem->Release();
            fileDialog->Release();

            if (!result)
            {
                return result;
            }

            *a_outWasSelected = true;
            return Result::ok();
        }
    }

    Result pick_directory_dialog(
        std::string_view a_title,
        std::string_view a_initialDirectory,
        std::string* a_outSelectedDirectory,
        bool* a_outWasSelected
    ) noexcept
    {
        if (a_outSelectedDirectory == nullptr || a_outWasSelected == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Output parameter must not be null.");
        }

        *a_outSelectedDirectory = "";
        *a_outWasSelected = false;

        struct DialogState final
        {
            Result result = Result::ok();
            std::string selectedDirectory{};
            bool wasSelected = false;
        };

        DialogState state{};

        try
        {
            std::thread dialogThread([&state, title = std::string(a_title),
                initialDirectory = std::string(a_initialDirectory)]() mutable
                {
                    const HRESULT initHr =
                        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
                    {
                        state.result = Result::fail(
                            convert_hresult_code(initHr), Severity::Error,
                            "Failed to initialize COM for directory picker dialog.");
                        return;
                    }

                    state.result = pick_directory_dialog_impl(
                        title,
                        initialDirectory,
                        &state.selectedDirectory,
                        &state.wasSelected
                    );

                    if (SUCCEEDED(initHr))
                    {
                        ::CoUninitialize();
                    }
                });
            dialogThread.join();
        }
        catch (...)
        {
            return Result::fail(
                Code::InternalError, Severity::Error,
                "Failed to create directory picker dialog thread.");
        }

        if (!state.result)
        {
            return state.result;
        }

        *a_outSelectedDirectory = std::move(state.selectedDirectory);
        *a_outWasSelected = state.wasSelected;
        return Result::ok();
    }
}
