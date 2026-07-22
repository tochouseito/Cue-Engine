#include "GameScriptBuildRunner.h"

// === Core includes ===
#include <IO/IFileSystem.h>

// === Windows includes ===
#include <windows.h>

// === C++ includes ===
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
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
                                    "GameScript path UTF-8 conversion failed.");
            }

            a_outText.assign(static_cast<size_t>(requiredLength), L'\0');
            const int writtenLength = ::MultiByteToWideChar(
                CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()),
                a_outText.data(), requiredLength);
            if (writtenLength != requiredLength)
            {
                return Result::fail(Code::InternalError, Severity::Error,
                                    "GameScript path UTF-8 conversion was incomplete.");
            }

            return Result::ok();
        }

        [[nodiscard]] const char* get_build_preset(
            std::string_view a_configuration) noexcept
        {
            if (a_configuration == "Debug")
            {
                return "win-x64-debug";
            }
            if (a_configuration == "Release")
            {
                return "win-x64-release";
            }

            return nullptr;
        }

        void read_log(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_logPath,
            ScriptBuildReport& a_inOutReport)
        {
            std::vector<std::byte> data{};
            const Result result = a_fileSystem.read_all(a_logPath, &data);
            if (!result)
            {
                a_inOutReport.output = "Build log could not be read.";
                return;
            }
            if (data.empty())
            {
                a_inOutReport.output.clear();
                return;
            }

            a_inOutReport.output.assign(
                reinterpret_cast<const char*>(data.data()), data.size());
        }
    } // namespace

    GameScriptBuildRunner::GameScriptBuildRunner(
        Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(&a_fileSystem)
    {
    }

    Result GameScriptBuildRunner::configure(
        const Core::IO::Path& a_scriptRoot,
        ScriptBuildReport& a_outReport) const noexcept
    {
        return run_cmake(a_scriptRoot, L"cmake.exe --preset win-x64 --fresh",
                         "Configure.log", true, a_outReport);
    }

    Result GameScriptBuildRunner::build(
        const Core::IO::Path& a_scriptRoot,
        std::string_view a_configuration,
        ScriptBuildReport& a_outReport) const noexcept
    {
        const char* buildPreset = get_build_preset(a_configuration);
        if (buildPreset == nullptr)
        {
            return Result::fail(Code::Unsupported, Severity::Warning,
                                "GameScript build configuration is not supported.");
        }
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "GameScript build runner is not initialized.");
        }

        const Core::IO::Path cachePath = Core::IO::Path::join(
            a_scriptRoot, Core::IO::Path("Intermediate/CMake/win-x64/CMakeCache.txt"));
        bool hasConfigureCache = false;
        Result result = m_fileSystem->exists(cachePath, &hasConfigureCache);
        if (!result)
        {
            return result;
        }

        std::string configureOutput{};
        if (!hasConfigureCache)
        {
            result = configure(a_scriptRoot, a_outReport);
            if (!result)
            {
                return result;
            }
            configureOutput = std::move(a_outReport.output);
        }

        const std::wstring commandLine =
            L"cmake.exe --build --preset " + std::wstring(buildPreset, buildPreset + std::strlen(buildPreset));
        result = run_cmake(a_scriptRoot, commandLine, "Build.log", hasConfigureCache == false,
                           a_outReport);
        if (!configureOutput.empty())
        {
            a_outReport.output = configureOutput + "\n\n" + a_outReport.output;
        }
        if (result)
        {
            a_outReport.summary = "GameScript build succeeded.";
        }
        return result;
    }

    Result GameScriptBuildRunner::run_cmake(
        const Core::IO::Path& a_scriptRoot,
        std::wstring_view a_commandLine,
        std::string_view a_logFileName,
        bool a_didConfigure,
        ScriptBuildReport& a_outReport) const noexcept
    {
        a_outReport = {};
        a_outReport.didConfigure = a_didConfigure;
        if (m_fileSystem == nullptr || a_scriptRoot.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                                "GameScript root path is empty.");
        }

        bool hasCMakeLists = false;
        Result result = m_fileSystem->exists(
            Core::IO::Path::join(a_scriptRoot, Core::IO::Path("CMakeLists.txt")), &hasCMakeLists);
        if (!result)
        {
            return result;
        }
        if (!hasCMakeLists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                                "GameScript CMakeLists.txt was not found.");
        }

        const Core::IO::Path logDirectory = Core::IO::Path::join(
            a_scriptRoot, Core::IO::Path("Intermediate/BuildSystem"));
        result = m_fileSystem->create_directories(logDirectory);
        if (!result)
        {
            return result;
        }

        a_outReport.logPath = Core::IO::Path::join(
            logDirectory, Core::IO::Path(std::string(a_logFileName)));
        std::wstring wideScriptRoot{};
        result = utf8_to_wide(a_scriptRoot.normalize().utf8(), wideScriptRoot);
        if (!result)
        {
            return result;
        }

        std::wstring wideLogPath{};
        result = utf8_to_wide(a_outReport.logPath.normalize().utf8(), wideLogPath);
        if (!result)
        {
            return result;
        }

        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;
        const HANDLE logHandle = ::CreateFileW(
            wideLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &securityAttributes,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logHandle == INVALID_HANDLE_VALUE)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                                "GameScript build log could not be created.");
        }

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = logHandle;
        startupInfo.hStdOutput = logHandle;
        startupInfo.hStdError = logHandle;

        PROCESS_INFORMATION processInfo{};
        std::vector<wchar_t> commandLine(a_commandLine.begin(), a_commandLine.end());
        commandLine.push_back(L'\0');
        const BOOL wasCreated = ::CreateProcessW(
            nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, wideScriptRoot.c_str(), &startupInfo, &processInfo);
        ::CloseHandle(logHandle);
        if (wasCreated == FALSE)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                                "GameScript CMake process could not be started.");
        }

        const DWORD waitResult = ::WaitForSingleObject(processInfo.hProcess, INFINITE);
        if (waitResult != WAIT_OBJECT_0)
        {
            ::CloseHandle(processInfo.hThread);
            ::CloseHandle(processInfo.hProcess);
            return Result::fail(Code::InternalError, Severity::Error,
                                "GameScript CMake process wait failed.");
        }

        DWORD exitCode = 1u;
        const BOOL gotExitCode = ::GetExitCodeProcess(processInfo.hProcess, &exitCode);
        ::CloseHandle(processInfo.hThread);
        ::CloseHandle(processInfo.hProcess);
        a_outReport.exitCode = exitCode;
        read_log(*m_fileSystem, a_outReport.logPath, a_outReport);
        if (gotExitCode == FALSE || exitCode != 0u)
        {
            a_outReport.summary = "GameScript CMake command failed.";
            return Result::fail(Code::InternalError, Severity::Error,
                                "GameScript CMake command failed.");
        }

        a_outReport.succeeded = true;
        a_outReport.summary = a_didConfigure
                                  ? "GameScript CMake configure succeeded."
                                  : "GameScript build succeeded.";
        return Result::ok();
    }
} // namespace Cue::Editor
