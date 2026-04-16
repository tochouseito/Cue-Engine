#include "VisualStudioBuildRunner.h"

namespace Cue::Editor
{
    Result VisualStudioBuildRunner::plan_script_build(
        const ScriptBuildRequest&,
        ScriptBuildPlan& a_outPlan) const noexcept
    {
        a_outPlan = {};
        return Result::fail(Code::Unsupported, Severity::Error,
            "VisualStudio build planning is not implemented yet.");
    }

    Result VisualStudioBuildRunner::validate_script_build_environment(
        const ScriptBuildRequest&,
        ScriptBuildValidation& a_outValidation) const noexcept
    {
        a_outValidation = {};
        return Result::fail(Code::Unsupported, Severity::Error,
            "VisualStudio build validation is not implemented yet.");
    }

    Result VisualStudioBuildRunner::execute_script_build(
        const ScriptBuildRequest& a_request,
        BuildResult& a_outResult) const noexcept
    {
        return m_bridge.execute_script_build(a_request, a_outResult);
    }
}
