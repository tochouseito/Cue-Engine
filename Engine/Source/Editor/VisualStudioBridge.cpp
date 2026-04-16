#include "VisualStudioBridge.h"

namespace Cue::Editor
{
    Result VisualStudioBridge::execute_script_build(
        const ScriptBuildRequest&,
        BuildResult& a_outResult) const noexcept
    {
        a_outResult = {};
        a_outResult.succeeded = false;
        a_outResult.summary =
            "VisualStudioBridge はまだ実装されていません。";
        a_outResult.messages.push_back(BuildMessage{
            BuildMessageSeverity::Error,
            BuildStage::General,
            a_outResult.summary
        });
        return Result::fail(Code::Unsupported, Severity::Error,
            "VisualStudioBridge is not implemented yet.");
    }
}
