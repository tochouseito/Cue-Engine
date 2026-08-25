#include <Cue/Foundation/Fatal.h>

#include <cstdlib>
#include <utility>

namespace
{
[[noreturn]] void terminate_fatal(cue::FatalHandler &a_fatalHandler) noexcept
{
    a_fatalHandler.terminate();
    // Custom Handler が契約に反して復帰しても、回復不能な状態から実行を継続させない
    std::abort();
}

[[noreturn]] void terminate_emergency(cue::FatalHandler &a_fatalHandler, std::string_view a_message) noexcept
{
    a_fatalHandler.terminate(a_message);
    // Logger を使えない経路なので追加診断を試みず、Process 終了だけを保証する
    std::abort();
}
} // namespace

namespace cue
{
[[noreturn]] void AbortFatalHandler::terminate() noexcept
{
    std::abort();
}

[[noreturn]] void AbortFatalHandler::terminate(std::string_view a_message) noexcept
{
    static_cast<void>(a_message);
    std::abort();
}

[[noreturn]] void report_fatal(Logger &a_logger, FatalHandler &a_fatalHandler, std::string_view a_message,
                               std::source_location a_location) noexcept
{
    const LogResult result = a_logger.log_and_flush(a_message, a_location);
    if (result == LogResult::Contended)
    {
        // Fatal 中の Lock 待機は Deadlock になり得るため、診断量より確実な終了を優先する
        terminate_emergency(a_fatalHandler, "Fatal logging unavailable due to contention");
    }
    // SinkFailure でも出力は可能な範囲で試行済みのため、回復不能な Process を必ず終了する
    terminate_fatal(a_fatalHandler);
}

[[noreturn]] void report_fatal(Logger &a_logger, FatalHandler &a_fatalHandler, std::string_view a_message,
                               Error &&a_error, std::source_location a_location) noexcept
{
    const LogResult result = a_logger.log_and_flush(a_message, std::move(a_error), a_location);
    if (result == LogResult::Contended)
    {
        // Fatal 中の Lock 待機は Deadlock になり得るため、診断量より確実な終了を優先する
        terminate_emergency(a_fatalHandler, "Fatal logging unavailable due to contention");
    }
    // SinkFailure でも出力は可能な範囲で試行済みのため、回復不能な Process を必ず終了する
    terminate_fatal(a_fatalHandler);
}
} // namespace cue
