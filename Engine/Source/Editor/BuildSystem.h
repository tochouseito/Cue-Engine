#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/Path.h>

// === C++ includes ===
#include <cstdint>
#include <string>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    enum class BuildConfiguration : uint8_t
    {
        Debug,
        RelWithDebInfo,
        Release
    };

    struct ScriptBuildRequest final
    {
        Core::IO::Path scriptRoot{};
        std::string configurePreset = "win-x64";
        BuildConfiguration configuration = BuildConfiguration::Debug;
        std::string target = "GameScript";
    };

    struct ScriptBuildPlan final
    {
        Core::IO::Path scriptRoot{};
        Core::IO::Path buildDirectory{};
        Core::IO::Path presetsPath{};
        std::string configureCommand{};
        std::string buildCommand{};
        bool requiresConfigure = false;
    };

    class BuildSystem final
    {
    public:
        explicit BuildSystem(Core::IO::IFileSystem& a_fileSystem) noexcept;
        ~BuildSystem() = default;

        [[nodiscard]] Result plan_script_build(
            const ScriptBuildRequest& a_request,
            ScriptBuildPlan& a_outPlan) const noexcept;

        [[nodiscard]] static const char* to_configuration_name(
            BuildConfiguration a_configuration) noexcept;
        [[nodiscard]] static std::string to_build_preset_name(
            BuildConfiguration a_configuration);

    private:
        Core::IO::IFileSystem& m_fileSystem;
    };
}
