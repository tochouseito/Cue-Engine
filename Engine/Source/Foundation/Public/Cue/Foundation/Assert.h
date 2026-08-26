#pragma once

#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>

#include <source_location>
#include <string_view>

#ifndef CUE_ENABLE_ASSERTS
#error CUE_ENABLE_ASSERTS must be provided by the Cue.Foundation CMake target
#endif

#ifndef CUE_ENABLE_DEBUG_BREAK
#error CUE_ENABLE_DEBUG_BREAK must be provided by the Cue.Foundation CMake target
#endif

namespace cue
{
/// @brief Assert 失敗時に Debugger へ制御を移すための非所有 Callback
using debugBreakCallback = void (*)() noexcept;

/// @brief Assert 診断に必要な非所有 Context
///
/// Assert Macro を特定の Global Logger へ結合せず、実行環境ごとに診断先と終了方法を注入するために使用する
/// Logger と Fatal Handler の Owner は Context より長く生存させる
class AssertContext final
{
  public:
    /// @brief Assert 診断で共有する非所有依存を束ねる
    /// @param a_logger 失敗 Record の同期出力先
    /// @param a_fatalHandler 診断後の Process 終了境界
    /// @param a_tryBreak Debugger 停止が利用可能な場合の任意 Callback
    AssertContext(Logger &a_logger, FatalHandler &a_fatalHandler, debugBreakCallback a_tryBreak = nullptr) noexcept;

    /// @brief Logger を返す
    [[nodiscard]] Logger &logger() const noexcept;

    /// @brief Fatal Handler を返す
    [[nodiscard]] FatalHandler &fatal_handler() const noexcept;

    /// @brief Debugger Break Callback があれば試行する
    void try_break() const noexcept;

  private:
    Logger *m_logger;
    FatalHandler *m_fatalHandler;
    debugBreakCallback m_tryBreak;
};

/// @brief Assert 失敗を Fatal として診断し Process を終了する
///
/// 通常の Fatal 経路と統合することで、Assert だけが異なる Log 形式や終了規則を持つことを防ぐ
[[noreturn]] void report_assert_failure(const AssertContext &a_context, std::string_view a_message,
                                        std::source_location a_location = std::source_location::current()) noexcept;
} // namespace cue

#if CUE_ENABLE_ASSERTS
/// @brief Macro 展開位置を SourceLocation へ残し、失敗した Condition を一度だけ評価する
#define CUE_ASSERT(a_context, a_condition, a_message)                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(a_condition))                                                                                            \
        {                                                                                                              \
            ::cue::report_assert_failure((a_context), (a_message), std::source_location::current());                   \
        }                                                                                                              \
    } while (false)
#else
#define CUE_ASSERT(a_context, a_condition, a_message) ((void)0)
#endif
