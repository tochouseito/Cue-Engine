#pragma once

// === Editor includes ===
#include "IBuildRunner.h"
#include "VisualStudioBridge.h"

namespace Cue::Editor
{
    class VisualStudioBuildRunner final : public IBuildRunner
    {
    public:
        VisualStudioBuildRunner() = default;
        ~VisualStudioBuildRunner() override = default;

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
        VisualStudioBridge m_bridge{};
    };
}
