#include "BuildSystem.h"

// === Editor includes ===
#include "CMakeBuildRunner.h"
#include "IBuildRunner.h"
#include "VisualStudioBuildRunner.h"

namespace Cue::Editor
{
    BuildSystem::BuildSystem(Core::IO::IFileSystem& a_fileSystem) noexcept
        : m_fileSystem(a_fileSystem)
        , m_cmakeBuildRunner(std::make_unique<CMakeBuildRunner>(a_fileSystem))
        , m_visualStudioBuildRunner(std::make_unique<VisualStudioBuildRunner>(a_fileSystem))
    {
    }

    BuildSystem::~BuildSystem() = default;

    Result BuildSystem::plan_script_build(
        const ScriptBuildRequest& a_request,
        ScriptBuildPlan& a_outPlan) const noexcept
    {
        const IBuildRunner* runner = resolve_runner(a_request.backend);
        if (runner == nullptr)
        {
            return Result::fail(Code::Unsupported, Severity::Error,
                "対応していない build backend です。");
        }

        return runner->plan_script_build(a_request, a_outPlan);
    }

    Result BuildSystem::validate_script_build_environment(
        const ScriptBuildRequest& a_request,
        ScriptBuildValidation& a_outValidation) const noexcept
    {
        const IBuildRunner* runner = resolve_runner(a_request.backend);
        if (runner == nullptr)
        {
            return Result::fail(Code::Unsupported, Severity::Error,
                "対応していない build backend です。");
        }

        return runner->validate_script_build_environment(a_request, a_outValidation);
    }

    Result BuildSystem::execute_script_build(
        const ScriptBuildRequest& a_request,
        BuildResult& a_outResult) const noexcept
    {
        const IBuildRunner* runner = resolve_runner(a_request.backend);
        if (runner == nullptr)
        {
            a_outResult = {};
            a_outResult.summary = "対応していない build backend です。";
            a_outResult.messages.push_back(BuildMessage{
                BuildMessageSeverity::Error,
                BuildStage::General,
                a_outResult.summary
            });
            return Result::fail(Code::Unsupported, Severity::Error,
                "Unsupported build backend.");
        }

        return runner->execute_script_build(a_request, a_outResult);
    }

    Result BuildSystem::execute_script_configure(
        const ScriptBuildRequest& a_request,
        BuildResult& a_outResult) const noexcept
    {
        const IBuildRunner* runner = resolve_runner(a_request.backend);
        if (runner == nullptr)
        {
            a_outResult = {};
            a_outResult.summary = "対応していない build backend です。";
            a_outResult.messages.push_back(BuildMessage{
                BuildMessageSeverity::Error,
                BuildStage::General,
                a_outResult.summary
            });
            return Result::fail(Code::Unsupported, Severity::Error,
                "Unsupported build backend.");
        }

        return runner->execute_script_configure(a_request, a_outResult);
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

    const IBuildRunner* BuildSystem::resolve_runner(
        BuildBackend a_backend) const noexcept
    {
        switch (a_backend)
        {
        case BuildBackend::CMake:
            return m_cmakeBuildRunner.get();

        case BuildBackend::VisualStudio:
            return m_visualStudioBuildRunner.get();
        }

        return nullptr;
    }
}
