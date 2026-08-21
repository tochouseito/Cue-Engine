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
using debugBreakCallback = void (*)() noexcept;

/**
 * @brief Assert診断に必要な非所有Context
 *
 * LoggerとFatal HandlerのOwnerはContextより長く生存させる
 */
class AssertContext final
{
  public:
    AssertContext(Logger &a_logger, FatalHandler &a_fatalHandler, debugBreakCallback a_tryBreak = nullptr) noexcept;

    /** @brief Loggerを返す */
    [[nodiscard]] Logger &logger() const noexcept;

    /** @brief Fatal Handlerを返す */
    [[nodiscard]] FatalHandler &fatal_handler() const noexcept;

    /** @brief Debugger Break Callbackがあれば試行する */
    void try_break() const noexcept;

  private:
    Logger *m_logger;
    FatalHandler *m_fatalHandler;
    debugBreakCallback m_tryBreak;
};

/** @brief Assert失敗をFatalとして診断しProcessを終了する */
[[noreturn]] void report_assert_failure(const AssertContext &a_context, std::string_view a_message,
                                        std::source_location a_location = std::source_location::current()) noexcept;
} // namespace cue

#if CUE_ENABLE_ASSERTS
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
