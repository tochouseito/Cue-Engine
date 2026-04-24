#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    class IBuildRunner;

    enum class BuildConfiguration : uint8_t
    {
        Debug,
        RelWithDebInfo,
        Release
    };

    enum class BuildBackend : uint8_t
    {
        CMake,
        VisualStudio
    };

    enum class BuildStage : uint8_t
    {
        General,
        Configure,
        Build,
        Reload,
        Attach
    };

    enum class BuildMessageSeverity : uint8_t
    {
        Info,
        Warning,
        Error
    };

    struct ScriptBuildRequest final
    {
        Core::IO::Path scriptRoot{};
        std::string configurePreset = "win-x64";
        BuildConfiguration configuration = BuildConfiguration::Debug;
        std::string target = "GameScript";
        BuildBackend backend = BuildBackend::CMake;
    };

    struct ScriptBuildPlan final
    {
        Core::IO::Path scriptRoot{};
        Core::IO::Path configureDirectory{};
        Core::IO::Path buildDirectory{};
        Core::IO::Path presetsPath{};
        std::string configureCommand{};
        std::string buildCommand{};
        bool requiresConfigure = false;
    };

    struct ScriptBuildValidation final
    {
        Core::IO::Path scriptRoot{};
        Core::IO::Path presetsPath{};
        std::string cmakePath{};
        std::string vcpkgRoot{};
        bool requiresVcpkgRoot = false;
    };

    struct BuildStageResult final
    {
        BuildStage stage = BuildStage::General;
        std::string command{};
        std::string output{};
        Core::IO::Path logPath{};
        uint32_t exitCode = 0;
        bool succeeded = false;
    };

    struct BuildMessage final
    {
        BuildMessageSeverity severity = BuildMessageSeverity::Info;
        BuildStage stage = BuildStage::General;
        std::string text{};
    };

    struct BuildArtifact final
    {
        std::string name{};
        Core::IO::Path path{};
    };

    struct BuildResult final
    {
        ScriptBuildPlan plan{};
        std::vector<BuildStageResult> stageResults{};
        std::vector<BuildMessage> messages{};
        std::vector<BuildArtifact> artifacts{};
        Core::IO::Path configureLogPath{};
        Core::IO::Path buildLogPath{};
        std::string summary{};
        uint32_t exitCode = 0;
        bool succeeded = false;
        bool didConfigure = false;
    };

    struct GameReleaseBuildRequest final
    {
        Core::IO::Path projectRoot{};
        std::string configurePreset = "win-x64";
        BuildConfiguration configuration = BuildConfiguration::Release;
        std::string gameTarget = "Game";
        std::string appTarget = "CueApp";
        BuildBackend backend = BuildBackend::CMake;
    };

    struct GameReleaseBuildPlan final
    {
        Core::IO::Path projectRoot{};
        Core::IO::Path configureDirectory{};
        Core::IO::Path buildDirectory{};
        Core::IO::Path presetsPath{};
        std::string configureCommand{};
        std::string buildCommand{};
        bool requiresConfigure = false;
    };

    struct GameReleaseBuildValidation final
    {
        Core::IO::Path projectRoot{};
        Core::IO::Path presetsPath{};
        std::string cmakePath{};
        std::string vcpkgRoot{};
        bool requiresVcpkgRoot = false;
    };

    struct GameReleaseBuildResult final
    {
        GameReleaseBuildPlan plan{};
        std::vector<BuildStageResult> stageResults{};
        std::vector<BuildMessage> messages{};
        std::vector<BuildArtifact> artifacts{};
        Core::IO::Path configureLogPath{};
        Core::IO::Path buildLogPath{};
        std::string summary{};
        uint32_t exitCode = 0;
        bool succeeded = false;
        bool didConfigure = false;
    };

    class BuildSystem final
    {
    public:
        explicit BuildSystem(Core::IO::IFileSystem& a_fileSystem) noexcept;
        ~BuildSystem();

        [[nodiscard]] Result plan_script_build(
            const ScriptBuildRequest& a_request,
            ScriptBuildPlan& a_outPlan) const noexcept;
        [[nodiscard]] Result validate_script_build_environment(
            const ScriptBuildRequest& a_request,
            ScriptBuildValidation& a_outValidation) const noexcept;
        [[nodiscard]] Result execute_script_configure(
            const ScriptBuildRequest& a_request,
            BuildResult& a_outResult) const noexcept;
        [[nodiscard]] Result execute_script_build(
            const ScriptBuildRequest& a_request,
            BuildResult& a_outResult) const noexcept;
        [[nodiscard]] Result plan_game_release_build(
            const GameReleaseBuildRequest& a_request,
            GameReleaseBuildPlan& a_outPlan) const noexcept;
        [[nodiscard]] Result validate_game_release_build_environment(
            const GameReleaseBuildRequest& a_request,
            GameReleaseBuildValidation& a_outValidation) const noexcept;
        [[nodiscard]] Result execute_game_release_configure(
            const GameReleaseBuildRequest& a_request,
            GameReleaseBuildResult& a_outResult) const noexcept;
        [[nodiscard]] Result execute_game_release_build(
            const GameReleaseBuildRequest& a_request,
            GameReleaseBuildResult& a_outResult) const noexcept;

        [[nodiscard]] static const char* to_configuration_name(
            BuildConfiguration a_configuration) noexcept;
        [[nodiscard]] static std::string to_build_preset_name(
            BuildConfiguration a_configuration);

    private:
        [[nodiscard]] const IBuildRunner* resolve_runner(
            BuildBackend a_backend) const noexcept;

    private:
        Core::IO::IFileSystem& m_fileSystem;
        std::unique_ptr<IBuildRunner> m_cmakeBuildRunner = nullptr;
        std::unique_ptr<IBuildRunner> m_visualStudioBuildRunner = nullptr;
    };
}
