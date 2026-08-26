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
/// @brief 診断 Record の重要度を共通表現で Sink へ伝える Level
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

/// @brief Logger から Sink へ同期的に渡す Move-only Record
///
/// Message、呼び出し位置、任意の Error を一つの寿命へまとめ、Sink 処理中の参照切れを防ぐ
class LogRecord final
{
  public:
    /// @brief LogRecord の一意所有を保つため Copy 構築を禁止する
    LogRecord(const LogRecord &) = delete;
    /// @brief LogRecord の一意所有を保つため Copy 代入を禁止する
    LogRecord &operator=(const LogRecord &) = delete;
    /// @brief LogRecord の状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    LogRecord(LogRecord &&) noexcept = default;
    /// @brief LogRecord の状態を Move 代入し、移動元は有効だが内容未規定の状態にする
    LogRecord &operator=(LogRecord &&) noexcept = default;
    /// @brief LogRecord が保持する Resource を所有権規則に従って破棄する
    ~LogRecord() = default;

    /// @brief Log Level を返す
    [[nodiscard]] LogLevel level() const noexcept;

    /// @brief Message を返す
    [[nodiscard]] std::string_view message() const noexcept;

    /// @brief 呼び出し元の Source 位置を返す
    [[nodiscard]] const SourceLocation &location() const noexcept;

    /// @brief Error があれば非所有 Pointer を返す
    [[nodiscard]] const Error *try_error() const noexcept;

  private:
    friend class Logger;

    /// @brief LogRecord を必要な依存と初期状態から構築する
    LogRecord(LogLevel a_level, std::string &&a_message, SourceLocation a_location,
              std::optional<Error> &&a_error) noexcept;

    std::string m_message;
    std::optional<Error> m_error;
    SourceLocation m_location;
    LogLevel m_level;
};

/// @brief Logger が一意所有する同期出力先
///
/// Sink の寿命と呼び出し順を Logger へ集約し、複数出力先へ同じ Record を直列に配信する
/// `write` と `flush` は例外を投げず、Worker へ処理を委譲せず、Logger へ再入しない
class LogSink
{
  public:
    /// @brief 派生 Sink が出力先を実装するための基底状態を構築する
    LogSink() = default;
    /// @brief 基底 Pointer を介して派生 Log Sink を正しく破棄できるようにする
    virtual ~LogSink() = default;

    /// @brief LogSink の一意所有を保つため Copy 構築を禁止する
    LogSink(const LogSink &) = delete;
    /// @brief LogSink の一意所有を保つため Copy 代入を禁止する
    LogSink &operator=(const LogSink &) = delete;
    /// @brief LogSink の所有状態を移動させないため Move 構築を禁止する
    LogSink(LogSink &&) = delete;
    /// @brief LogSink の所有状態を移動させないため Move 代入を禁止する
    LogSink &operator=(LogSink &&) = delete;

    /// @brief Record を同期出力する
    /// @return 出力に成功した場合は true
    [[nodiscard]] virtual bool write(const LogRecord &a_record) = 0;

    /// @brief 保留中の出力を同期 Flush する
    [[nodiscard]] virtual bool flush() = 0;
};

/// @brief 呼び出し側が診断経路の失敗を Exception なしで判定するための結果
enum class LogResult
{
    Success,
    SinkFailure,
    Contended
};

/// @brief Sink を一意所有して同期出力を直列化する Logger
///
/// Record 単位の出力順序を Mutex で保ち、異なる Thread の文字列が Sink 上で混在することを防ぐ
/// Public 操作は Thread Safe であり、破棄開始前に全呼び出しを完了させる
class Logger final
{
  public:
    /// @brief Logger を構築する
    /// @param a_emergencyHandler Logger より長く生存する非所有終了境界
    /// @param a_sinks Logger へ所有権を移す Sink 一覧
    Logger(EmergencyHandler &a_emergencyHandler, std::vector<std::unique_ptr<LogSink>> &&a_sinks) noexcept;
    /// @brief Logger が保持する Resource を所有権規則に従って破棄する
    ~Logger();

    /// @brief Logger の一意所有を保つため Copy 構築を禁止する
    Logger(const Logger &) = delete;
    /// @brief Logger の一意所有を保つため Copy 代入を禁止する
    Logger &operator=(const Logger &) = delete;
    /// @brief Logger の所有状態を移動させないため Move 構築を禁止する
    Logger(Logger &&) = delete;
    /// @brief Logger の所有状態を移動させないため Move 代入を禁止する
    Logger &operator=(Logger &&) = delete;

    /// @brief Message を全 Sink へ同期出力する
    [[nodiscard]] LogResult log(LogLevel a_level, std::string_view a_message,
                                std::source_location a_location = std::source_location::current()) noexcept;

    /// @brief Error 所有権を Record へ移して同期出力する
    [[nodiscard]] LogResult log(LogLevel a_level, std::string_view a_message, Error &&a_error,
                                std::source_location a_location = std::source_location::current()) noexcept;

    /// @brief 全 Sink を同期 Flush する
    [[nodiscard]] LogResult flush() noexcept;

    /// @brief Fatal Record の出力と Flush を一度の非待機 Lock で行う
    /// @return Lock 競合時は Contended
    ///
    /// Fatal 発生時に別 Thread が Logger を保持していても待機による停止不能へ陥らないため、Lock
    /// 取得失敗を呼び出し側へ返す
    [[nodiscard]] LogResult log_and_flush(std::string_view a_message,
                                          std::source_location a_location = std::source_location::current()) noexcept;

    /// @brief Error 所有権を Fatal Record へ移して出力と Flush を行う
    [[nodiscard]] LogResult log_and_flush(std::string_view a_message, Error &&a_error,
                                          std::source_location a_location = std::source_location::current()) noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/// @brief stdout または stderr へ同期出力する Sink
///
/// 外部 Log 基盤が未導入の初期化中や障害時にも、標準 Stream だけで最低限の診断を残す
class ConsoleLogSink final : public LogSink
{
  public:
    /// @brief ConsoleLogSink を必要な依存と初期状態から構築する
    ConsoleLogSink() = default;
    /// @brief ConsoleLogSink が保持する Resource を所有権規則に従って破棄する
    ~ConsoleLogSink() override = default;

    /// @brief 受け取った Log Record を対象 Sink へ書き込み、出力成否を返す
    [[nodiscard]] bool write(const LogRecord &a_record) noexcept override;
    /// @brief 対象 Sink に保留中の Log 出力を反映し、完了成否を返す
    [[nodiscard]] bool flush() noexcept override;
};
} // namespace cue
