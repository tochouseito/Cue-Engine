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
    if (m_tryBreak != nullptr)
    {
        m_tryBreak();
    }
}

[[noreturn]] void report_assert_failure(const AssertContext &a_context, std::string_view a_message,
                                        std::source_location a_location) noexcept
{
#if CUE_ENABLE_DEBUG_BREAK
    a_context.try_break();
#endif
    report_fatal(a_context.logger(), a_context.fatal_handler(), a_message, a_location);
}
} // namespace cue
