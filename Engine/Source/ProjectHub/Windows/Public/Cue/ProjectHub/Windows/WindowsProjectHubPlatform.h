#pragma once

#include <Cue/ProjectHub/Service.h>

#include <memory>

namespace cue
{
class AssertContext;
}

namespace cue::project_hub
{
/// @brief Windows File APIとUUID生成をProject Hub Platform境界へ接続する
[[nodiscard]] Result<std::unique_ptr<ProjectHubPlatform>> create_windows_project_hub_platform(
    const AssertContext &a_assertContext) noexcept;
} // namespace cue::project_hub
