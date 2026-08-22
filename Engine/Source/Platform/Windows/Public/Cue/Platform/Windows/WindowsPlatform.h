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
 * @param a_text 呼び出し中だけ参照し、返却後に保持しない UTF-16 文字列
 * @param a_assertContext 呼び出し完了まで有効である非所有診断 Context
 * @return 成功時は所有 UTF-8 文字列。無効な UTF-16、変換失敗、長さ Overflow は Native Error 付き Result
 *
 * 共有可変状態を持たないため、各引数と AssertContext の参照先が全呼び出し中に有効なら並行呼び出し可能
 * Allocation 失敗時は `a_assertContext.fatal_handler()` を呼び、Processを終了する
 */
[[nodiscard]] Result<std::string> convert_windows_argument_to_utf8(
    std::wstring_view a_text, const AssertContext &a_assertContext) noexcept;
} // namespace cue
