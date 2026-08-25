#include <Cue/Foundation/Log.h>

#include <cinttypes>
#include <cstdio>

namespace
{
// 外部 Tool がなくても人が重要度を識別できる安定した表示名へ変換する
[[nodiscard]] const char *level_name(cue::LogLevel a_level) noexcept
{
    switch (a_level)
    {
    case cue::LogLevel::Trace:
        return "Trace";
    case cue::LogLevel::Debug:
        return "Debug";
    case cue::LogLevel::Info:
        return "Info";
    case cue::LogLevel::Warning:
        return "Warning";
    case cue::LogLevel::Error:
        return "Error";
    case cue::LogLevel::Fatal:
        return "Fatal";
    }
    return "Unknown";
}

[[nodiscard]] bool write_view(FILE *a_stream, std::string_view a_value) noexcept
{
    // 一時的な null 終端 String を生成せず、障害時の追加 Allocation を避けて出力する
    return std::fwrite(a_value.data(), sizeof(char), a_value.size(), a_stream) == a_value.size();
}
} // namespace

namespace cue
{
bool ConsoleLogSink::write(const LogRecord &a_record) noexcept
{
    // Error 以上は通常出力と分離し、Shell や CI が stderr だけを監視する場合にも検出可能にする
    FILE *stream = a_record.level() == LogLevel::Error || a_record.level() == LogLevel::Fatal ? stderr : stdout;

    // 途中の出力に失敗しても残りを試行し、可能な範囲で一つの診断 Record を残す
    bool didSucceed = std::fputs("[", stream) >= 0;
    didSucceed = write_view(stream, level_name(a_record.level())) && didSucceed;
    didSucceed = std::fputs("] ", stream) >= 0 && didSucceed;
    didSucceed = write_view(stream, a_record.message()) && didSucceed;
    didSucceed = std::fputs(" (", stream) >= 0 && didSucceed;
    didSucceed = write_view(stream, a_record.location().file_name()) && didSucceed;
    didSucceed = std::fprintf(stream, ":%" PRIuLEAST32 " ", a_record.location().line()) >= 0 && didSucceed;
    didSucceed = write_view(stream, a_record.location().function_name()) && didSucceed;
    didSucceed = std::fputs(")\n", stream) >= 0 && didSucceed;

    if (const Error *error = a_record.try_error())
    {
        didSucceed = std::fputs("  Error: ", stream) >= 0 && didSucceed;
        didSucceed = write_view(stream, error->code().domain()) && didSucceed;
        didSucceed = std::fprintf(stream, "/%" PRId64 " ", error->code().value()) >= 0 && didSucceed;
        didSucceed = write_view(stream, error->summary()) && didSucceed;
        didSucceed = std::fputs("\n", stream) >= 0 && didSucceed;
    }

    return didSucceed;
}

bool ConsoleLogSink::flush() noexcept
{
    // Level によって出力先が分かれるため、終了前には両方の Stream を確定させる
    const bool didFlushOutput = std::fflush(stdout) == 0;
    const bool didFlushError = std::fflush(stderr) == 0;
    return didFlushOutput && didFlushError;
}
} // namespace cue
