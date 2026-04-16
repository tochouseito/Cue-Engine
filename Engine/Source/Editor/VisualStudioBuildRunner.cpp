#include "VisualStudioBuildRunner.h"

// === Core includes ===
#include <IO/IFileSystem.h>

// === Win includes ===
#include <Engine/Source/Runtime/PAL/Win/ConvertUTF.h>

// === C++ includes ===
#include <string_view>
#include <vector>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] Result resolve_executable_path(
            std::wstring_view a_fileName,
            std::string& a_outPath) noexcept
        {
            a_outPath.clear();

            wchar_t buffer[MAX_PATH]{};
            const DWORD length = ::SearchPathW(
                nullptr,
                a_fileName.data(),
                nullptr,
                static_cast<DWORD>(std::size(buffer)),
                buffer,
                nullptr);
            if (length == 0)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "dotnet.exe が見つかりません。PATH を確認してください。");
            }

            return PAL::Win::wide_to_utf8(
                std::wstring_view(buffer, length), &a_outPath);
        }

        [[nodiscard]] Core::IO::Path get_tool_project_path() noexcept
        {
#ifdef CUE_PROJECT_ROOT_PATH
            return Core::IO::Path(
                std::string(CUE_PROJECT_ROOT_PATH) +
                "/Tools/VisualStudioBridge/VisualStudioBridge.csproj");
#else
            return Core::IO::Path("Tools/VisualStudioBridge/VisualStudioBridge.csproj");
#endif
        }

        [[nodiscard]] bool is_solution_file(const Core::IO::Path& a_path) noexcept
        {
            const std::string extension = a_path.extension();
            return extension == ".sln" || extension == ".slnx";
        }

        void push_message(
            BuildResult& a_result,
            BuildMessageSeverity a_severity,
            BuildStage a_stage,
            std::string a_text)
        {
            a_result.messages.push_back(BuildMessage{
                a_severity,
                a_stage,
                std::move(a_text)
            });
        }
    }

    Result VisualStudioBuildRunner::plan_script_build(
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

        bool configureDirectoryExists = false;
        result = m_fileSystem.exists(configureDirectory, &configureDirectoryExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "VisualStudio configure ディレクトリの確認に失敗しました。");
        }

        bool hasSolution = false;
        if (configureDirectoryExists)
        {
            std::vector<Core::IO::Path> entries{};
            result = m_fileSystem.list_directory(configureDirectory, &entries);
            if (!result)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "VisualStudio solution の確認に失敗しました。");
            }

            for (const Core::IO::Path& entry : entries)
            {
                if (is_solution_file(entry))
                {
                    hasSolution = true;
                    break;
                }
            }
        }

        a_outPlan.scriptRoot = a_request.scriptRoot;
        a_outPlan.presetsPath = validation.presetsPath;
        a_outPlan.configureDirectory = configureDirectory;
        a_outPlan.buildDirectory = buildDirectory;
        a_outPlan.requiresConfigure = !hasSolution;
        a_outPlan.configureCommand =
            "cmake --preset " + a_request.configurePreset + " --fresh";
        a_outPlan.buildCommand =
            "MSBuild <solution> /t:" + a_request.target +
            " /p:Configuration=" +
            std::string(BuildSystem::to_configuration_name(a_request.configuration));
        return Result::ok();
    }

    Result VisualStudioBuildRunner::validate_script_build_environment(
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

        std::string dotnetPath{};
        result = resolve_executable_path(L"dotnet.exe", dotnetPath);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path toolProjectPath = get_tool_project_path();
        bool toolProjectExists = false;
        result = m_fileSystem.exists(toolProjectPath, &toolProjectExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "VisualStudioBridge.csproj の確認に失敗しました。");
        }

        if (!toolProjectExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "VisualStudioBridge.csproj が見つかりません。");
        }

        a_outValidation.scriptRoot = a_request.scriptRoot;
        a_outValidation.presetsPath = presetsPath;
        return Result::ok();
    }

    Result VisualStudioBuildRunner::execute_script_build(
        const ScriptBuildRequest& a_request,
        BuildResult& a_outResult) const noexcept
    {
        a_outResult = {};

        Result result = plan_script_build(a_request, a_outResult.plan);
        if (!result)
        {
            a_outResult.summary = result.message;
            push_message(a_outResult, BuildMessageSeverity::Error, BuildStage::General,
                std::string(result.message));
            return result;
        }

        const ScriptBuildPlan plan = a_outResult.plan;
        result = m_bridge.execute_script_build(a_request, a_outResult);
        a_outResult.plan = plan;
        if (!result && a_outResult.summary.empty())
        {
            a_outResult.summary = result.message;
            push_message(a_outResult, BuildMessageSeverity::Error, BuildStage::General,
                std::string(result.message));
        }

        return result;
    }
}
