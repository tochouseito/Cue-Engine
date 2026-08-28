#pragma once

#include <Cue/Platform/SystemCapabilities.h>
#include <Cue/Platform/WindowSystem.h>

#include <memory>
#include <string>
#include <string_view>

namespace cue
{
class AssertContext;

/// @brief Windows Window System の一意な所有権を生成する
///
/// Win32 型を Platform 非依存 API へ漏らさず、Runtime が WindowSystem 契約だけへ依存できるようにする
/// AssertContext とその参照先は、返された Window System と全 Window より長く生存させる
[[nodiscard]] Result<std::unique_ptr<WindowSystem>> create_windows_window_system(
    const AssertContext &a_assertContext) noexcept;

/// @brief Windows の UTF-16 Command Line Argument を Engine の UTF-8 文字列へ変換する
///
/// @param a_text 呼び出し中だけ参照し、返却後に保持しない UTF-16 文字列
/// @param a_assertContext 呼び出し完了まで有効である非所有診断 Context
/// @return 成功時は所有 UTF-8 文字列、無効な UTF-16、変換失敗、長さ Overflow は Native Error 付き Result
///
/// 共有可変状態を持たないため、各引数と AssertContext の参照先が全呼び出し中に有効なら並行呼び出し可能
/// Allocation 失敗時は `a_assertContext.fatal_handler()` を呼び、Process を終了する
[[nodiscard]] Result<std::string> convert_windows_argument_to_utf8(std::wstring_view a_text,
                                                                   const AssertContext &a_assertContext) noexcept;

/// @brief 現在MachineのWindows System CapabilityとNative失敗診断の配送結果を所有値で返す
///
/// 共有可変状態やCacheを持たないため任意Threadから並行呼び出しでき、各呼び出しは独立値を返す
/// 個別Query失敗は該当FieldをFailedにし、Snapshot全体を破棄しない
[[nodiscard]] SystemCapabilityQueryReport query_windows_system_capabilities(
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
