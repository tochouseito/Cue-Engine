#pragma once

// === Base includes ===
#include <Result.h>

// === Editor includes ===
#include "BuildSystem.h"

namespace Cue::Editor
{
    class VisualStudioBridge final
    {
    public:
        VisualStudioBridge() = default;
        ~VisualStudioBridge() = default;

        [[nodiscard]] Result execute_script_build(
            const ScriptBuildRequest& a_request,
            BuildResult& a_outResult) const noexcept;
    };
}
