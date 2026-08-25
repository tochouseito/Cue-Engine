#include <Cue/Foundation/Assert.h>

namespace cue
{
AssertContext::AssertContext(Logger &a_logger, FatalHandler &a_fatalHandler, debugBreakCallback a_tryBreak) noexcept
    : m_logger(&a_logger), m_fatalHandler(&a_fatalHandler), m_tryBreak(a_tryBreak)
{
}

Logger &AssertContext::logger() const noexcept
{
    return *m_logger;
}

FatalHandler &AssertContext::fatal_handler() const noexcept
{
    return *m_fatalHandler;
}

void AssertContext::try_break() const noexcept
{
    // Debugger 未接続の実行環境でも同じ Assert 経路を使用できるよう Callback を任意にする
    if (m_tryBreak != nullptr)
    {
        m_tryBreak();
    }
}

[[noreturn]] void report_assert_failure(const AssertContext &a_context, std::string_view a_message,
                                        std::source_location a_location) noexcept
{
#if CUE_ENABLE_DEBUG_BREAK
    // Fatal 終了の前に停止し、失敗時点の Stack と Local 状態を Debugger で調査できるようにする
    a_context.try_break();
#endif
    // Assert 専用の終了処理を持たず、Fatal と同じ Log Flush と終了保証を再利用する
    report_fatal(a_context.logger(), a_context.fatal_handler(), a_message, a_location);
}
} // namespace cue
