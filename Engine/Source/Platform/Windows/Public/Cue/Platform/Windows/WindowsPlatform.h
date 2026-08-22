#pragma once

#include <Cue/Platform/WindowSystem.h>

#include <memory>
#include <string>
#include <string_view>

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

/**
 * @brief Windows の UTF-16 Command Line Argument を Engine の UTF-8 文字列へ変換する
 *
 * 無効な UTF-16、変換失敗、長さ Overflow は Native Error 付き Result として返す
 */
[[nodiscard]] Result<std::string> convert_windows_argument_to_utf8(
    std::wstring_view a_text, const AssertContext &a_assertContext) noexcept;
} // namespace cue
