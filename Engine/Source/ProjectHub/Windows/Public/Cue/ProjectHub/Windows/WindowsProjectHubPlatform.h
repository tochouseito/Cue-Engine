#pragma once

#include <Cue/ProjectHub/Service.h>

#include <memory>
#include <string_view>

namespace cue
{
class AssertContext;
}

namespace cue::project_hub
{
/// @brief Windows File APIとUUID生成をProject Hub Platform境界へ接続する
[[nodiscard]] Result<std::unique_ptr<ProjectHubPlatform>> create_windows_project_hub_platform(
    const AssertContext &a_assertContext) noexcept;

/// @brief Editor Launch Requestを独立Windows ProcessのCommand Lineへ変換して起動する
[[nodiscard]] Result<void> launch_windows_editor_process(std::string_view a_editorExecutableLocator,
                                                         const EditorLaunchRequest &a_request,
                                                         const AssertContext &a_assertContext) noexcept;
} // namespace cue::project_hub
