#pragma once

// === Editor includes ===
#include "BuildSystem.h"

namespace Cue::Editor
{
    class IBuildRunner
    {
    public:
        virtual ~IBuildRunner() = default;

        [[nodiscard]] virtual Result plan_script_build(
            const ScriptBuildRequest& a_request,
            ScriptBuildPlan& a_outPlan) const noexcept = 0;
        [[nodiscard]] virtual Result validate_script_build_environment(
            const ScriptBuildRequest& a_request,
            ScriptBuildValidation& a_outValidation) const noexcept = 0;
        [[nodiscard]] virtual Result execute_script_configure(
            const ScriptBuildRequest& a_request,
            BuildResult& a_outResult) const noexcept = 0;
        [[nodiscard]] virtual Result execute_script_build(
            const ScriptBuildRequest& a_request,
            BuildResult& a_outResult) const noexcept = 0;
    };
}
