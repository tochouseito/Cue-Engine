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
        explicit VisualStudioBridge(Core::IO::IFileSystem& a_fileSystem) noexcept
            : m_fileSystem(a_fileSystem)
        {
        }
        ~VisualStudioBridge() = default;

        [[nodiscard]] Result execute_script_build(
            const ScriptBuildRequest& a_request,
            BuildResult& a_outResult) const noexcept;

    private:
        Core::IO::IFileSystem& m_fileSystem;
    };
}
