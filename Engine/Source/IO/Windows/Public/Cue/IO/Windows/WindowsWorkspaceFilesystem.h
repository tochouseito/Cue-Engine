#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/IO/WorkspaceFilesystem.h>

#include <memory>
#include <string_view>

namespace cue
{
class AssertContext;

/// @brief Absolute Windows DirectoryへRoot境界付きWorkspace列挙CapabilityをBindingする
/// @param a_rootPath BindingするAbsolute Windows DirectoryのUTF-8 Path
/// @param a_assertContext Workspaceが所有Copyする診断Context。参照先LoggerとFatalHandlerはWorkspaceより長生きさせる
[[nodiscard]] Result<std::unique_ptr<WorkspaceFilesystem>> create_windows_workspace_filesystem(
    std::string_view a_rootPath, const AssertContext &a_assertContext) noexcept;
} // namespace cue
