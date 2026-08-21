#include <Cue/Foundation/Log.h>

#include <cinttypes>
#include <cstdio>

namespace
{
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
    return std::fwrite(a_value.data(), sizeof(char), a_value.size(), a_stream) == a_value.size();
}
} // namespace

namespace cue
{
bool ConsoleLogSink::write(const LogRecord &a_record) noexcept
{
    FILE *stream = a_record.level() == LogLevel::Error || a_record.level() == LogLevel::Fatal ? stderr : stdout;

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
    const bool didFlushOutput = std::fflush(stdout) == 0;
    const bool didFlushError = std::fflush(stderr) == 0;
    return didFlushOutput && didFlushError;
}
} // namespace cue
