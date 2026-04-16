#include "BuildSystem.h"

// === Core includes ===
#include <IO/IFileSystem.h>

// === Win includes ===
#include <Engine/Source/Runtime/PAL/Win/ConvertUTF.h>

// === C++ includes ===
#include <span>
#include <string_view>
#include <vector>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] Result read_text_file(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            std::string& a_outText) noexcept
        {
            a_outText.clear();

            bool exists = false;
            Result result = a_fileSystem.exists(a_filePath, &exists);
            if (!result)
            {
                return result;
            }
            if (!exists)
            {
                return Result::ok();
            }

            std::vector<std::byte> data{};
            result = a_fileSystem.read_all(a_filePath, &data);
            if (!result)
            {
                return result;
            }

            a_outText.assign(
                reinterpret_cast<const char*>(data.data()),
                data.size());
            return Result::ok();
        }

        [[nodiscard]] Result resolve_executable_path(
            std::wstring_view a_fileName,
            std::string& a_outPath) noexcept
        {
            a_outPath.clear();

            wchar_t buffer[MAX_PATH]{};
            const DWORD length = ::SearchPathW(nullptr, a_fileName.data(), nullptr,
                static_cast<DWORD>(std::size(buffer)), buffer, nullptr);
            if (length == 0)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "cmake.exe が見つかりません。PATH を確認してください。");
            }

            return PAL::Win::wide_to_utf8(
                std::wstring_view(buffer, length), &a_outPath);
        }

        [[nodiscard]] Result read_environment_variable(
            std::wstring_view a_name,
            std::string& a_outValue) noexcept
        {
            a_outValue.clear();

            const DWORD requiredLength =
                ::GetEnvironmentVariableW(a_name.data(), nullptr, 0);
            if (requiredLength == 0)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "VCPKG_ROOT が設定されていません。");
            }

            std::vector<wchar_t> wideValue(requiredLength);
            const DWORD written = ::GetEnvironmentVariableW(
                a_name.data(), wideValue.data(), requiredLength);
            if (written == 0)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "環境変数の取得に失敗しました。");
            }

            return PAL::Win::wide_to_utf8(
                std::wstring_view(wideValue.data(), written), &a_outValue);
        }

        [[nodiscard]] Result execute_command(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_workingDirectory,
            std::string_view a_command,
            const Core::IO::Path& a_outputPath,
            CommandExecutionResult& a_outResult) noexcept
        {
            a_outResult = {};
            a_outResult.command = std::string(a_command);

            Result result = a_fileSystem.create_directories(a_outputPath.parent());
            if (!result)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    "ビルドログ用ディレクトリの作成に失敗しました。");
            }

            bool removed = false;
            result = a_fileSystem.remove(a_outputPath, &removed);
            if (!result && result.code != Code::NotFound)
            {
                return result;
            }

            std::wstring wideWorkingDirectory{};
            result = PAL::Win::utf8_to_wide(a_workingDirectory.utf8(),
                &wideWorkingDirectory);
            if (!result)
            {
                return result;
            }

            std::wstring wideCommand{};
            result = PAL::Win::utf8_to_wide(std::string(a_command), &wideCommand);
            if (!result)
            {
                return result;
            }

            std::wstring wideOutputPath{};
            result = PAL::Win::utf8_to_wide(a_outputPath.utf8(), &wideOutputPath);
            if (!result)
            {
                return result;
            }

            std::wstring commandLine =
                L"cmd.exe /d /c \"" + wideCommand + L" > \"" + wideOutputPath +
                L"\" 2>&1\"";
            std::vector<wchar_t> commandBuffer(
                commandLine.begin(), commandLine.end());
            commandBuffer.push_back(L'\0');

            STARTUPINFOW startupInfo{};
            startupInfo.cb = sizeof(startupInfo);

            PROCESS_INFORMATION processInfo{};
            const BOOL created = ::CreateProcessW(
                nullptr,
                commandBuffer.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                wideWorkingDirectory.c_str(),
                &startupInfo,
                &processInfo);
            if (created == FALSE)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    "ビルドプロセスの起動に失敗しました。");
            }

            const DWORD waitResult =
                ::WaitForSingleObject(processInfo.hProcess, INFINITE);
            if (waitResult != WAIT_OBJECT_0)
            {
                ::CloseHandle(processInfo.hThread);
                ::CloseHandle(processInfo.hProcess);
                return Result::fail(Code::InternalError, Severity::Error,
                    "ビルドプロセスの完了待機に失敗しました。");
            }

            DWORD exitCode = 0;
            const BOOL gotExitCode =
                ::GetExitCodeProcess(processInfo.hProcess, &exitCode);

            ::CloseHandle(processInfo.hThread);
            ::CloseHandle(processInfo.hProcess);

            if (gotExitCode == FALSE)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "ビルドプロセスの終了コード取得に失敗しました。");
            }

            a_outResult.exitCode = static_cast<uint32_t>(exitCode);
            result = read_text_file(a_fileSystem, a_outputPath, a_outResult.output);
            if (!result)
            {
                return result;
            }

            if (exitCode != 0)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "ビルドコマンドの実行に失敗しました。");
            }

            return Result::ok();
        }
    }

    BuildSystem::BuildSystem(Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(a_fileSystem)
    {
    }

    Result BuildSystem::plan_script_build(
        const ScriptBuildRequest& a_request,
        ScriptBuildPlan& a_outPlan) const noexcept
    {
        a_outPlan = {};

        ScriptBuildValidation validation{};
        Result result =
            validate_script_build_environment(a_request, validation);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path configureDirectory = Core::IO::Path::join(
            a_request.scriptRoot,
            Core::IO::Path("out/build/" + a_request.configurePreset));
        const Core::IO::Path buildDirectory = Core::IO::Path::join(
            configureDirectory,
            Core::IO::Path(a_request.target));
        bool buildDirectoryExists = false;
        result = m_fileSystem.exists(configureDirectory, &buildDirectoryExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Script build ディレクトリの確認に失敗しました。");
        }

        a_outPlan.scriptRoot = a_request.scriptRoot;
        a_outPlan.presetsPath = validation.presetsPath;
        a_outPlan.configureDirectory = configureDirectory;
        a_outPlan.buildDirectory = buildDirectory;
        a_outPlan.requiresConfigure = !buildDirectoryExists;
        a_outPlan.configureCommand =
            "cmake --preset " + a_request.configurePreset;
        a_outPlan.buildCommand =
            "cmake --build out/build/" + a_request.configurePreset + "/" +
            a_request.target +
            " --config " + to_configuration_name(a_request.configuration) +
            " --target " + a_request.target;
        return Result::ok();
    }

    Result BuildSystem::validate_script_build_environment(
        const ScriptBuildRequest& a_request,
        ScriptBuildValidation& a_outValidation) const noexcept
    {
        a_outValidation = {};

        if (a_request.scriptRoot.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script root が指定されていません。");
        }

        bool scriptRootExists = false;
        Result result = m_fileSystem.exists(a_request.scriptRoot, &scriptRootExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Script root の確認に失敗しました。");
        }

        if (!scriptRootExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Script root が存在しません。");
        }

        Core::IO::FileStat scriptRootStat{};
        result = m_fileSystem.stat(a_request.scriptRoot, &scriptRootStat);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Script root の情報取得に失敗しました。");
        }

        if (scriptRootStat.type != Core::IO::FileType::directory)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script root はディレクトリである必要があります。");
        }

        const Core::IO::Path presetsPath = Core::IO::Path::join(
            a_request.scriptRoot, Core::IO::Path("CMakePresets.json"));
        bool presetsExists = false;
        result = m_fileSystem.exists(presetsPath, &presetsExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "CMakePresets.json の確認に失敗しました。");
        }

        if (!presetsExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "CMakePresets.json が見つかりません。");
        }

        std::string presetsText{};
        result = read_text_file(m_fileSystem, presetsPath, presetsText);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "CMakePresets.json の読み込みに失敗しました。");
        }

        std::string cmakePath{};
        result = resolve_executable_path(L"cmake.exe", cmakePath);
        if (!result)
        {
            return result;
        }

        a_outValidation.scriptRoot = a_request.scriptRoot;
        a_outValidation.presetsPath = presetsPath;
        a_outValidation.cmakePath = std::move(cmakePath);
        a_outValidation.requiresVcpkgRoot =
            presetsText.find("$env{VCPKG_ROOT}") != std::string::npos;

        if (!a_outValidation.requiresVcpkgRoot)
        {
            return Result::ok();
        }

        std::string vcpkgRoot{};
        result = read_environment_variable(L"VCPKG_ROOT", vcpkgRoot);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path vcpkgRootPath(vcpkgRoot);
        bool vcpkgRootExists = false;
        result = m_fileSystem.exists(vcpkgRootPath, &vcpkgRootExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "VCPKG_ROOT の確認に失敗しました。");
        }

        if (!vcpkgRootExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "VCPKG_ROOT が指すディレクトリが存在しません。");
        }

        Core::IO::FileStat vcpkgRootStat{};
        result = m_fileSystem.stat(vcpkgRootPath, &vcpkgRootStat);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "VCPKG_ROOT の情報取得に失敗しました。");
        }

        if (vcpkgRootStat.type != Core::IO::FileType::directory)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "VCPKG_ROOT はディレクトリである必要があります。");
        }

        a_outValidation.vcpkgRoot = std::move(vcpkgRoot);
        return Result::ok();
    }

    Result BuildSystem::execute_script_build(
        const ScriptBuildRequest& a_request,
        ScriptBuildResult& a_outResult) const noexcept
    {
        a_outResult = {};

        Result result = plan_script_build(a_request, a_outResult.plan);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path logRoot = Core::IO::Path::join(
            a_request.scriptRoot,
            Core::IO::Path("Intermediate/BuildSystem"));

        if (a_outResult.plan.requiresConfigure)
        {
            a_outResult.didConfigure = true;
            result = execute_command(
                m_fileSystem,
                a_request.scriptRoot,
                a_outResult.plan.configureCommand,
                Core::IO::Path::join(logRoot, Core::IO::Path("Configure.log")),
                a_outResult.configureStep);
            if (!result)
            {
                return result;
            }
        }

        result = execute_command(
            m_fileSystem,
            a_request.scriptRoot,
            a_outResult.plan.buildCommand,
            Core::IO::Path::join(logRoot, Core::IO::Path("Build.log")),
            a_outResult.buildStep);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    const char* BuildSystem::to_configuration_name(
        BuildConfiguration a_configuration) noexcept
    {
        switch (a_configuration)
        {
        case BuildConfiguration::Debug:
            return "Debug";

        case BuildConfiguration::RelWithDebInfo:
            return "RelWithDebInfo";

        case BuildConfiguration::Release:
            return "Release";
        }

        return "Debug";
    }

    std::string BuildSystem::to_build_preset_name(
        BuildConfiguration a_configuration)
    {
        switch (a_configuration)
        {
        case BuildConfiguration::Debug:
            return "win-x64-debug";

        case BuildConfiguration::RelWithDebInfo:
            return "win-x64-relwithdebinfo";

        case BuildConfiguration::Release:
            return "win-x64-release";
        }

        return "win-x64-debug";
    }
}
