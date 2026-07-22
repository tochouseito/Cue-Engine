#include "VisualStudioLauncher.h"

// === Runtime includes ===
#include <IO/Path.h>

// === Windows includes ===
#include <windows.h>

// === C++ includes ===
#include <cstddef>
#include <string>
#include <vector>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] Result utf8_to_wide(
            std::string_view a_text,
            std::wstring& a_outText) noexcept
        {
            const int requiredLength = ::MultiByteToWideChar(
                CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
            if (requiredLength <= 0)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                                    "Script path UTF-8 conversion failed.");
            }

            a_outText.assign(static_cast<size_t>(requiredLength), L'\0');
            const int writtenLength = ::MultiByteToWideChar(
                CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()),
                a_outText.data(), requiredLength);
            if (writtenLength != requiredLength)
            {
                return Result::fail(Code::InternalError, Severity::Error,
                                    "Script path UTF-8 conversion was incomplete.");
            }

            return Result::ok();
        }

        [[nodiscard]] bool is_regular_file(const std::wstring& a_path) noexcept
        {
            const DWORD attributes = ::GetFileAttributesW(a_path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                   (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
        }

        [[nodiscard]] bool is_directory(const std::wstring& a_path) noexcept
        {
            const DWORD attributes = ::GetFileAttributesW(a_path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                   (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
        }

        [[nodiscard]] Result validate_game_script_project_root(
            const std::wstring& a_projectRoot) noexcept
        {
            if (!is_directory(a_projectRoot))
            {
                return Result::fail(Code::NotFound, Severity::Warning,
                                    "GameScript project root was not found.");
            }

            const std::wstring cmakeListsPath = a_projectRoot + L"\\CMakeLists.txt";
            if (!is_regular_file(cmakeListsPath))
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Warning,
                    "GameScript CMakeLists.txt was not found.");
            }

            return Result::ok();
        }

        [[nodiscard]] Result find_dotnet(std::wstring& a_outPath) noexcept
        {
            wchar_t path[MAX_PATH]{};
            const DWORD pathLength = ::SearchPathW(
                nullptr, L"dotnet.exe", nullptr, MAX_PATH, path, nullptr);
            if (pathLength == 0u || pathLength >= MAX_PATH)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                                    "dotnet.exe was not found.");
            }

            a_outPath.assign(path, pathLength);
            return Result::ok();
        }

        [[nodiscard]] std::wstring bridge_project_path()
        {
            return std::wstring(CUE_EDITOR_ROOT_PATH) +
                   L"\\Tools\\VisualStudioBridge\\VisualStudioBridge.csproj";
        }

        [[nodiscard]] Result run_visual_studio_bridge(
            const std::wstring& a_projectRoot,
            const std::wstring* a_sourcePath) noexcept
        {
            const std::wstring projectPath = bridge_project_path();
            if (!is_regular_file(projectPath))
            {
                return Result::fail(Code::NotFound, Severity::Error,
                                    "VisualStudioBridge project was not found.");
            }

            std::wstring dotnetPath{};
            Result result = find_dotnet(dotnetPath);
            if (!result)
            {
                return result;
            }

            const wchar_t* command = a_sourcePath == nullptr
                                         ? L"open-project"
                                         : L"open-script";
            std::wstring commandLine = L"\"" + dotnetPath +
                                       L"\" run --project \"" + projectPath +
                                       L"\" --configuration Debug -- " + command +
                                       L" \"" + a_projectRoot + L"\"";
            if (a_sourcePath != nullptr)
            {
                commandLine += L" \"" + *a_sourcePath + L"\"";
            }

            std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
            commandBuffer.push_back(L'\0');

            STARTUPINFOW startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            PROCESS_INFORMATION processInfo{};
            const BOOL created = ::CreateProcessW(
                dotnetPath.c_str(), commandBuffer.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);
            if (created == FALSE)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                                    "VisualStudioBridge process could not be created.");
            }

            constexpr DWORD k_bridgeTimeoutMilliseconds = 120000u;
            const DWORD waitResult = ::WaitForSingleObject(
                processInfo.hProcess, k_bridgeTimeoutMilliseconds);
            if (waitResult == WAIT_TIMEOUT)
            {
                // Editor が helper の停止待ちで固まらないよう、所有する process だけを終了する。
                ::TerminateProcess(processInfo.hProcess, 1u);
                ::WaitForSingleObject(processInfo.hProcess, 5000u);
            }

            DWORD exitCode = 1u;
            const BOOL hasExitCode = ::GetExitCodeProcess(processInfo.hProcess, &exitCode);
            ::CloseHandle(processInfo.hThread);
            ::CloseHandle(processInfo.hProcess);
            if (waitResult != WAIT_OBJECT_0 || hasExitCode == FALSE || exitCode != 0u)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                                    "VisualStudioBridge could not open the GameScript CMake project.");
            }

            return Result::ok();
        }
    } // namespace

    Result open_script_asset_in_visual_studio(
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_scriptProjectRoot) noexcept
    {
        if (a_sourcePath.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Warning,
                                "Script source path is empty.");
        }

        std::wstring sourcePath{};
        Result result = utf8_to_wide(a_sourcePath.normalize().utf8(), sourcePath);
        if (!result)
        {
            return result;
        }
        if (!is_regular_file(sourcePath))
        {
            return Result::fail(Code::NotFound, Severity::Warning,
                                "Script source file was not found.");
        }

        std::wstring projectRoot{};
        result = utf8_to_wide(a_scriptProjectRoot.normalize().utf8(), projectRoot);
        if (!result)
        {
            return result;
        }

        result = validate_game_script_project_root(projectRoot);
        if (!result)
        {
            return result;
        }

        return run_visual_studio_bridge(projectRoot, &sourcePath);
    }

    Result open_script_project_in_visual_studio(
        const Core::IO::Path& a_projectRoot) noexcept
    {
        if (a_projectRoot.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Warning,
                                "GameScript project root is empty.");
        }

        std::wstring projectRoot{};
        Result result = utf8_to_wide(a_projectRoot.normalize().utf8(), projectRoot);
        if (!result)
        {
            return result;
        }

        result = validate_game_script_project_root(projectRoot);
        if (!result)
        {
            return result;
        }

        return run_visual_studio_bridge(projectRoot, nullptr);
    }
} // namespace Cue::Editor
