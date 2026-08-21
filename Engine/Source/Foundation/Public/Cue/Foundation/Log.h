#pragma once

#include <Cue/Foundation/EmergencyHandler.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/SourceLocation.h>

#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace cue
{
/** @brief Logの重要度 */
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

/** @brief LoggerからSinkへ同期的に渡すMove-only Record */
class LogRecord final
{
  public:
    LogRecord(const LogRecord &) = delete;
    LogRecord &operator=(const LogRecord &) = delete;
    LogRecord(LogRecord &&) noexcept = default;
    LogRecord &operator=(LogRecord &&) noexcept = default;
    ~LogRecord() = default;

    /** @brief Log Levelを返す */
    [[nodiscard]] LogLevel level() const noexcept;

    /** @brief Messageを返す */
    [[nodiscard]] std::string_view message() const noexcept;

    /** @brief 呼び出し元のSource位置を返す */
    [[nodiscard]] const SourceLocation &location() const noexcept;

    /** @brief Errorがあれば非所有Pointerを返す */
    [[nodiscard]] const Error *try_error() const noexcept;

  private:
    friend class Logger;

    LogRecord(LogLevel a_level, std::string &&a_message, SourceLocation a_location,
              std::optional<Error> &&a_error) noexcept;

    std::string m_message;
    std::optional<Error> m_error;
    SourceLocation m_location;
    LogLevel m_level;
};

/**
 * @brief Loggerが一意所有する同期出力先
 *
 * `write`と`flush`は例外を投げず、Workerへ処理を委譲せず、Loggerへ再入しない
 */
class LogSink
{
  public:
    LogSink() = default;
    virtual ~LogSink() = default;

    LogSink(const LogSink &) = delete;
    LogSink &operator=(const LogSink &) = delete;
    LogSink(LogSink &&) = delete;
    LogSink &operator=(LogSink &&) = delete;

    /**
     * @brief Recordを同期出力する
     * @return 出力に成功した場合はtrue
     */
    [[nodiscard]] virtual bool write(const LogRecord &a_record) = 0;

    /** @brief 保留中の出力を同期Flushする */
    [[nodiscard]] virtual bool flush() = 0;
};

/** @brief Logger操作の非例外結果 */
enum class LogResult
{
    Success,
    SinkFailure,
    Contended
};

/**
 * @brief Sinkを一意所有して同期出力を直列化するLogger
 *
 * Public操作はThread Safe。破棄開始前に全呼び出しを完了させる
 */
class Logger final
{
  public:
    /**
     * @brief Loggerを構築する
     * @param a_emergencyHandler Loggerより長く生存する非所有終了境界
     * @param a_sinks Loggerへ所有権を移すSink一覧
     */
    Logger(EmergencyHandler &a_emergencyHandler, std::vector<std::unique_ptr<LogSink>> &&a_sinks) noexcept;
    ~Logger();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    /** @brief Messageを全Sinkへ同期出力する */
    [[nodiscard]] LogResult log(LogLevel a_level, std::string_view a_message,
                                std::source_location a_location = std::source_location::current()) noexcept;

    /** @brief Error所有権をRecordへ移して同期出力する */
    [[nodiscard]] LogResult log(LogLevel a_level, std::string_view a_message, Error &&a_error,
                                std::source_location a_location = std::source_location::current()) noexcept;

    /** @brief 全Sinkを同期Flushする */
    [[nodiscard]] LogResult flush() noexcept;

    /**
     * @brief Fatal Recordの出力とFlushを一度の非待機Lockで行う
     * @return Lock競合時はContended
     */
    [[nodiscard]] LogResult log_and_flush(std::string_view a_message,
                                          std::source_location a_location = std::source_location::current()) noexcept;

    /** @brief Error所有権をFatal Recordへ移して出力とFlushを行う */
    [[nodiscard]] LogResult log_and_flush(std::string_view a_message, Error &&a_error,
                                          std::source_location a_location = std::source_location::current()) noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/** @brief stdoutまたはstderrへ同期出力するSink */
class ConsoleLogSink final : public LogSink
{
  public:
    ConsoleLogSink() = default;
    ~ConsoleLogSink() override = default;

    [[nodiscard]] bool write(const LogRecord &a_record) noexcept override;
    [[nodiscard]] bool flush() noexcept override;
};
} // namespace cue
