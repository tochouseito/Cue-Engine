#include <Cue/Foundation/Log.h>

#include <cstdlib>
#include <mutex>
#include <utility>

namespace
{
thread_local unsigned int sinkDispatchDepth = 0;

[[noreturn]] void terminate_emergency(cue::EmergencyHandler &a_emergencyHandler, std::string_view a_message) noexcept
{
    a_emergencyHandler.terminate(a_message);
    std::abort();
}

class SinkDispatchGuard final
{
  public:
    SinkDispatchGuard() noexcept
    {
        ++sinkDispatchDepth;
    }

    ~SinkDispatchGuard()
    {
        --sinkDispatchDepth;
    }

    SinkDispatchGuard(const SinkDispatchGuard &) = delete;
    SinkDispatchGuard &operator=(const SinkDispatchGuard &) = delete;
    SinkDispatchGuard(SinkDispatchGuard &&) = delete;
    SinkDispatchGuard &operator=(SinkDispatchGuard &&) = delete;
};
} // namespace

namespace cue
{
class Logger::Impl final
{
  public:
    Impl(EmergencyHandler &a_emergencyHandler, std::vector<std::unique_ptr<LogSink>> &&a_sinks) noexcept
        : emergencyHandler(a_emergencyHandler), sinks(std::move(a_sinks))
    {
    }

    [[nodiscard]] LogResult write(const LogRecord &a_record)
    {
        LogResult result = LogResult::Success;
        for (const std::unique_ptr<LogSink> &sink : sinks)
        {
            SinkDispatchGuard guard;
            if (!sink->write(a_record))
            {
                result = LogResult::SinkFailure;
            }
        }
        return result;
    }

    [[nodiscard]] LogResult flush_sinks()
    {
        LogResult result = LogResult::Success;
        for (const std::unique_ptr<LogSink> &sink : sinks)
        {
            SinkDispatchGuard guard;
            if (!sink->flush())
            {
                result = LogResult::SinkFailure;
            }
        }
        return result;
    }

    EmergencyHandler &emergencyHandler;
    std::vector<std::unique_ptr<LogSink>> sinks;
    std::mutex mutex;
};

LogRecord::LogRecord(LogLevel a_level, std::string &&a_message, SourceLocation a_location,
                     std::optional<Error> &&a_error) noexcept
    : m_message(std::move(a_message)), m_error(std::move(a_error)), m_location(a_location), m_level(a_level)
{
}

LogLevel LogRecord::level() const noexcept
{
    return m_level;
}

std::string_view LogRecord::message() const noexcept
{
    return m_message;
}

const SourceLocation &LogRecord::location() const noexcept
{
    return m_location;
}

const Error *LogRecord::try_error() const noexcept
{
    return m_error ? &m_error.value() : nullptr;
}

Logger::Logger(EmergencyHandler &a_emergencyHandler, std::vector<std::unique_ptr<LogSink>> &&a_sinks) noexcept
{
    for (const std::unique_ptr<LogSink> &sink : a_sinks)
    {
        if (sink == nullptr)
        {
            terminate_emergency(a_emergencyHandler, "Logger received a null sink");
        }
    }

    try
    {
        m_impl = std::make_unique<Impl>(a_emergencyHandler, std::move(a_sinks));
    }
    catch (...)
    {
        terminate_emergency(a_emergencyHandler, "Logger creation failed");
    }
}

Logger::~Logger() = default;

LogResult Logger::log(LogLevel a_level, std::string_view a_message, std::source_location a_location) noexcept
{
    if (sinkDispatchDepth != 0)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger sink reentry detected");
    }

    try
    {
        LogRecord record(a_level, std::string(a_message), SourceLocation::from(a_location), std::nullopt);
        const std::lock_guard lock(m_impl->mutex);
        return m_impl->write(record);
    }
    catch (...)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger operation failed");
    }
}

LogResult Logger::log(LogLevel a_level, std::string_view a_message, Error &&a_error,
                      std::source_location a_location) noexcept
{
    if (sinkDispatchDepth != 0)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger sink reentry detected");
    }

    try
    {
        LogRecord record(a_level, std::string(a_message), SourceLocation::from(a_location),
                         std::optional<Error>(std::move(a_error)));
        const std::lock_guard lock(m_impl->mutex);
        return m_impl->write(record);
    }
    catch (...)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger operation failed");
    }
}

LogResult Logger::flush() noexcept
{
    if (sinkDispatchDepth != 0)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger sink reentry detected");
    }

    try
    {
        const std::lock_guard lock(m_impl->mutex);
        return m_impl->flush_sinks();
    }
    catch (...)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger flush failed");
    }
}

LogResult Logger::log_and_flush(std::string_view a_message, std::source_location a_location) noexcept
{
    if (sinkDispatchDepth != 0)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger sink reentry detected");
    }

    try
    {
        LogRecord record(LogLevel::Fatal, std::string(a_message), SourceLocation::from(a_location), std::nullopt);
        const std::unique_lock lock(m_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock())
        {
            return LogResult::Contended;
        }

        const LogResult writeResult = m_impl->write(record);
        const LogResult flushResult = m_impl->flush_sinks();
        return writeResult == LogResult::Success && flushResult == LogResult::Success ? LogResult::Success
                                                                                      : LogResult::SinkFailure;
    }
    catch (...)
    {
        terminate_emergency(m_impl->emergencyHandler, "Fatal logger operation failed");
    }
}

LogResult Logger::log_and_flush(std::string_view a_message, Error &&a_error, std::source_location a_location) noexcept
{
    if (sinkDispatchDepth != 0)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger sink reentry detected");
    }

    try
    {
        LogRecord record(LogLevel::Fatal, std::string(a_message), SourceLocation::from(a_location),
                         std::optional<Error>(std::move(a_error)));
        const std::unique_lock lock(m_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock())
        {
            return LogResult::Contended;
        }

        const LogResult writeResult = m_impl->write(record);
        const LogResult flushResult = m_impl->flush_sinks();
        return writeResult == LogResult::Success && flushResult == LogResult::Success ? LogResult::Success
                                                                                      : LogResult::SinkFailure;
    }
    catch (...)
    {
        terminate_emergency(m_impl->emergencyHandler, "Fatal logger operation failed");
    }
}
} // namespace cue
