#pragma once

#include <Cue/Foundation/EmergencyHandler.h>
#include <Cue/Foundation/SourceLocation.h>

#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cue
{
/**
 * @brief Domain内で一意なError識別子
 *
 * Move-only Valueとして呼び出し側が所有する
 */
class ErrorCode final
{
  public:
    ErrorCode(const ErrorCode &) = delete;
    ErrorCode &operator=(const ErrorCode &) = delete;
    ErrorCode(ErrorCode &&) noexcept = default;
    ErrorCode &operator=(ErrorCode &&) noexcept = default;
    ~ErrorCode() = default;

    /**
     * @brief Error Codeを生成する
     * @param a_emergencyHandler Allocation失敗時の非所有終了境界
     * @param a_domain Code Domain
     * @param a_value Domain内のCode
     */
    [[nodiscard]] static ErrorCode create(EmergencyHandler &a_emergencyHandler, std::string_view a_domain,
                                          std::int64_t a_value) noexcept;

    /** @brief Code Domainを返す */
    [[nodiscard]] std::string_view domain() const noexcept;

    /** @brief Domain内のCodeを返す */
    [[nodiscard]] std::int64_t value() const noexcept;

  private:
    ErrorCode(std::string &&a_domain, std::int64_t a_value) noexcept;

    std::string m_domain;
    std::int64_t m_value;
};

/**
 * @brief Native Headerへ依存しないNative Error情報
 */
class NativeError final
{
  public:
    NativeError(const NativeError &) = delete;
    NativeError &operator=(const NativeError &) = delete;
    NativeError(NativeError &&) noexcept = default;
    NativeError &operator=(NativeError &&) noexcept = default;
    ~NativeError() = default;

    /**
     * @brief Native Error情報を生成する
     */
    [[nodiscard]] static NativeError create(EmergencyHandler &a_emergencyHandler, std::string_view a_domain,
                                            std::int64_t a_value) noexcept;

    /** @brief Native Domainを返す */
    [[nodiscard]] std::string_view domain() const noexcept;

    /** @brief Native Codeを返す */
    [[nodiscard]] std::int64_t value() const noexcept;

  private:
    NativeError(std::string &&a_domain, std::int64_t a_value) noexcept;

    std::string m_domain;
    std::int64_t m_value;
};

/**
 * @brief Error伝播時に追加された診断Context
 */
class ErrorContext final
{
  public:
    ErrorContext(const ErrorContext &) = delete;
    ErrorContext &operator=(const ErrorContext &) = delete;
    ErrorContext(ErrorContext &&) noexcept = default;
    ErrorContext &operator=(ErrorContext &&) noexcept = default;
    ~ErrorContext() = default;

    /** @brief Context Messageを返す */
    [[nodiscard]] std::string_view message() const noexcept;

    /** @brief Context追加位置を返す */
    [[nodiscard]] const SourceLocation &location() const noexcept;

  private:
    friend class Error;

    ErrorContext(std::string &&a_message, SourceLocation a_location) noexcept;

    std::string m_message;
    SourceLocation m_location;
};

/**
 * @brief 再分類前のErrorを保持する非再帰Cause Frame
 */
class ErrorCause final
{
  public:
    ErrorCause(const ErrorCause &) = delete;
    ErrorCause &operator=(const ErrorCause &) = delete;
    ErrorCause(ErrorCause &&) noexcept = default;
    ErrorCause &operator=(ErrorCause &&) noexcept = default;
    ~ErrorCause() = default;

    /** @brief 再分類前のError Codeを返す */
    [[nodiscard]] const ErrorCode &code() const noexcept;

    /** @brief 再分類前のSummaryを返す */
    [[nodiscard]] std::string_view summary() const noexcept;

    /** @brief 再分類前のContextを返す */
    [[nodiscard]] std::span<const ErrorContext> contexts() const noexcept;

    /** @brief Native Errorがあれば非所有Pointerを返す */
    [[nodiscard]] const NativeError *try_native_error() const noexcept;

  private:
    friend class Error;

    ErrorCause(ErrorCode &&a_code, std::string &&a_summary, std::vector<ErrorContext> &&a_contexts,
               std::optional<NativeError> &&a_nativeError) noexcept;

    ErrorCode m_code;
    std::string m_summary;
    std::vector<ErrorContext> m_contexts;
    std::optional<NativeError> m_nativeError;
};

/**
 * @brief 診断可能なMove-only Error Value
 *
 * Immutable参照は任意Threadで利用できる
 * Mutationは外部同期し、HandlerのOwnerをErrorより長く生存させる
 */
class Error final
{
  public:
    Error(const Error &) = delete;
    Error &operator=(const Error &) = delete;
    Error(Error &&) noexcept = default;
    Error &operator=(Error &&) noexcept = default;
    ~Error() = default;

    /** @brief Native ErrorなしのErrorを生成する */
    [[nodiscard]] static Error create(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                      std::string_view a_summary) noexcept;

    /** @brief Native Error付きErrorを生成する */
    [[nodiscard]] static Error create(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                      std::string_view a_summary, NativeError &&a_nativeError) noexcept;

    /**
     * @brief Errorを新しい抽象Levelへ再分類する
     * @param a_cause 消費される直前のError
     */
    [[nodiscard]] static Error reclassify(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                          std::string_view a_summary, Error &&a_cause) noexcept;

    /**
     * @brief 伝播Contextを追加する
     * @param a_emergencyHandler Allocation失敗時の非所有終了境界
     * @param a_message Context Message
     * @param a_location Context追加位置
     */
    void add_context(EmergencyHandler &a_emergencyHandler, std::string_view a_message,
                     std::source_location a_location = std::source_location::current()) noexcept;

    /** @brief Primary Error Codeを返す */
    [[nodiscard]] const ErrorCode &code() const noexcept;

    /** @brief 開発者向けSummaryを返す */
    [[nodiscard]] std::string_view summary() const noexcept;

    /** @brief 伝播Contextを追加順で返す */
    [[nodiscard]] std::span<const ErrorContext> contexts() const noexcept;

    /** @brief Native Errorがあれば非所有Pointerを返す */
    [[nodiscard]] const NativeError *try_native_error() const noexcept;

    /** @brief Immediate CauseからRoot Causeまでを順に返す */
    [[nodiscard]] std::span<const ErrorCause> causes() const noexcept;

    /** @brief Root Cause Codeを返す */
    [[nodiscard]] const ErrorCode &root_code() const noexcept;

  private:
    Error(ErrorCode &&a_code, std::string &&a_summary, std::optional<NativeError> &&a_nativeError) noexcept;

    ErrorCode m_code;
    std::string m_summary;
    std::vector<ErrorContext> m_contexts;
    std::optional<NativeError> m_nativeError;
    std::vector<ErrorCause> m_causes;
};
} // namespace cue
