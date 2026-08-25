#include <Cue/Foundation/Log.h>

#include <cstdlib>
#include <mutex>
#include <utility>

namespace
{
// Sink から同じ Thread の Logger へ再入すると Mutex 再取得で停止するため、Dispatch 中かを Thread 単位で追跡する
thread_local unsigned int sinkDispatchDepth = 0;

[[noreturn]] void terminate_emergency(cue::EmergencyHandler &a_emergencyHandler, std::string_view a_message) noexcept
{
    a_emergencyHandler.terminate(a_message);
    // Emergency Handler の実装違反があっても、診断基盤が壊れた状態で実行を継続させない
    std::abort();
}

// Exception や早期 Return が発生しても再入検出状態を元へ戻す Scope Guard
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
        // 一つの Sink 失敗で後続 Sink を止めず、利用可能な診断先へ Record を残し続ける
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
        // 各 Sink を独立して Flush し、一部障害時にも他の保留 Record を可能な限り確定する
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
    // 所有権移動後に無効 Sink が見つからないよう、Logger 状態を作る前に入力全体を検証する
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
    // Sink 実装からの再入は非再帰 Mutex を停止させるため、Lock 取得前に明示的な契約違反として扱う
    if (sinkDispatchDepth != 0)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger sink reentry detected");
    }

    try
    {
        // Allocation を Lock 取得前に完了し、Sink を直列化する Critical Section を出力処理だけに限定する
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
    // Sink 実装からの再入は非再帰 Mutex を停止させるため、Lock 取得前に明示的な契約違反として扱う
    if (sinkDispatchDepth != 0)
    {
        terminate_emergency(m_impl->emergencyHandler, "Logger sink reentry detected");
    }

    try
    {
        // Error 所有権を Record へ移してから Lock し、Critical Section 内での構築失敗を避ける
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
    // write と同じ直列化境界を使用し、Record 出力中に Sink 状態を Flush しないようにする
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
        // Fatal 経路では Lock 待機による Deadlock を避け、競合を Emergency 終了へ引き渡す
        const std::unique_lock lock(m_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock())
        {
            return LogResult::Contended;
        }

        // 同じ Lock 保持中に write と flush を完了し、Fatal Record より後の出力が割り込むことを防ぐ
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
        // Fatal 経路では Lock 待機による Deadlock を避け、競合を Emergency 終了へ引き渡す
        const std::unique_lock lock(m_impl->mutex, std::try_to_lock);
        if (!lock.owns_lock())
        {
            return LogResult::Contended;
        }

        // Error 詳細を含む Record も同じ Lock 内で Flush し、終了直前の診断を一つの単位として確定する
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
