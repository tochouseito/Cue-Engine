#include "BuildSystem.h"

// === Core includes ===
#include <IO/IFileSystem.h>

namespace Cue::Editor
{
    BuildSystem::BuildSystem(Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(a_fileSystem)
    {
    }

    Result BuildSystem::plan_script_build(
        const ScriptBuildRequest& a_request,
        ScriptBuildPlan& a_outPlan) const noexcept
    {
        a_outPlan = {};

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

        const Core::IO::Path buildDirectory = Core::IO::Path::join(
            a_request.scriptRoot,
            Core::IO::Path("out/build/" + a_request.configurePreset));
        bool buildDirectoryExists = false;
        result = m_fileSystem.exists(buildDirectory, &buildDirectoryExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Script build ディレクトリの確認に失敗しました。");
        }

        a_outPlan.scriptRoot = a_request.scriptRoot;
        a_outPlan.presetsPath = presetsPath;
        a_outPlan.buildDirectory = buildDirectory;
        a_outPlan.requiresConfigure = !buildDirectoryExists;
        a_outPlan.configureCommand =
            "cmake --preset " + a_request.configurePreset;
        a_outPlan.buildCommand =
            "cmake --build out/build/" + a_request.configurePreset +
            " --config " + to_configuration_name(a_request.configuration) +
            " --target " + a_request.target;
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
