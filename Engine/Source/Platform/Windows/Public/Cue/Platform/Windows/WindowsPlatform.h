#pragma once

#include <Cue/Platform/WindowSystem.h>

#include <memory>

namespace cue
{
class AssertContext;

/**
 * @brief Windows Window System の一意な所有権を生成する
 *
 * AssertContext とその参照先は、返された Window System と全 Window より長く生存させる
 */
[[nodiscard]] Result<std::unique_ptr<WindowSystem>> create_windows_window_system(
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
