// VisualStudioBridge の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === Editor includes ===
#include "BuildSystem.h"

// === C++ includes ===
#include <string_view>

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
        [[nodiscard]] Result open_solution(
            const Core::IO::Path& a_scriptRoot,
            std::string_view a_configurePreset) const noexcept;
        [[nodiscard]] Result attach_debugger(
            const Core::IO::Path& a_scriptRoot,
            std::string_view a_configurePreset,
            uint32_t a_processId) const noexcept;

    private:
        Core::IO::IFileSystem& m_fileSystem;
    };
}
