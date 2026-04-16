#pragma once

// === Editor includes ===
#include "IBuildRunner.h"

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    class CMakeBuildRunner final : public IBuildRunner
    {
    public:
        explicit CMakeBuildRunner(Core::IO::IFileSystem& a_fileSystem) noexcept;
        ~CMakeBuildRunner() override = default;

        [[nodiscard]] Result plan_script_build(
            const ScriptBuildRequest& a_request,
            ScriptBuildPlan& a_outPlan) const noexcept override;
        [[nodiscard]] Result validate_script_build_environment(
            const ScriptBuildRequest& a_request,
            ScriptBuildValidation& a_outValidation) const noexcept override;
        [[nodiscard]] Result execute_script_build(
            const ScriptBuildRequest& a_request,
            BuildResult& a_outResult) const noexcept override;

    private:
        Core::IO::IFileSystem& m_fileSystem;
    };
}
