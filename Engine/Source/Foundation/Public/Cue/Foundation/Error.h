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
 * @brief Domain 内で一意な Error 識別子
 *
 * Domain と数値を分離し、Module 間で同じ数値を使用しても Error の意味が衝突しないようにする
 * Move-only Value として呼び出し側が所有する
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
     * @brief Error Code を生成する
     * @param a_emergencyHandler Allocation 失敗時の非所有終了境界
     * @param a_domain Code Domain
     * @param a_value Domain 内の Code
     */
    [[nodiscard]] static ErrorCode create(EmergencyHandler &a_emergencyHandler, std::string_view a_domain,
                                          std::int64_t a_value) noexcept;

    /** @brief Code Domain を返す */
    [[nodiscard]] std::string_view domain() const noexcept;

    /** @brief Domain 内の Code を返す */
    [[nodiscard]] std::int64_t value() const noexcept;

  private:
    ErrorCode(std::string &&a_domain, std::int64_t a_value) noexcept;

    std::string m_domain;
    std::int64_t m_value;
};

/**
 * @brief Native Header へ依存しない Native Error 情報
 *
 * Win32 や Graphics API の型を Foundation 公開 API へ漏らさず、元の低 Level 診断値を保持する
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
     * @brief Native Error 情報を生成する
     */
    [[nodiscard]] static NativeError create(EmergencyHandler &a_emergencyHandler, std::string_view a_domain,
                                            std::int64_t a_value) noexcept;

    /** @brief Native Domain を返す */
    [[nodiscard]] std::string_view domain() const noexcept;

    /** @brief Native Code を返す */
    [[nodiscard]] std::int64_t value() const noexcept;

  private:
    NativeError(std::string &&a_domain, std::int64_t a_value) noexcept;

    std::string m_domain;
    std::int64_t m_value;
};

/**
 * @brief Error 伝播時に追加された診断 Context
 *
 * Error の分類を変えずに、失敗が通過した処理と Source 位置を積み重ねる
 */
class ErrorContext final
{
  public:
    ErrorContext(const ErrorContext &) = delete;
    ErrorContext &operator=(const ErrorContext &) = delete;
    ErrorContext(ErrorContext &&) noexcept = default;
    ErrorContext &operator=(ErrorContext &&) noexcept = default;
    ~ErrorContext() = default;

    /** @brief Context Message を返す */
    [[nodiscard]] std::string_view message() const noexcept;

    /** @brief Context 追加位置を返す */
    [[nodiscard]] const SourceLocation &location() const noexcept;

  private:
    friend class Error;

    ErrorContext(std::string &&a_message, SourceLocation a_location) noexcept;

    std::string m_message;
    SourceLocation m_location;
};

/**
 * @brief 再分類前の Error を保持する非再帰 Cause Frame
 *
 * Error を再帰所有せず平坦な配列へ格納し、Cause Chain の寿命と走査順を単純に保つ
 */
class ErrorCause final
{
  public:
    ErrorCause(const ErrorCause &) = delete;
    ErrorCause &operator=(const ErrorCause &) = delete;
    ErrorCause(ErrorCause &&) noexcept = default;
    ErrorCause &operator=(ErrorCause &&) noexcept = default;
    ~ErrorCause() = default;

    /** @brief 再分類前の Error Code を返す */
    [[nodiscard]] const ErrorCode &code() const noexcept;

    /** @brief 再分類前の Summary を返す */
    [[nodiscard]] std::string_view summary() const noexcept;

    /** @brief 再分類前の Context を返す */
    [[nodiscard]] std::span<const ErrorContext> contexts() const noexcept;

    /** @brief Native Error があれば非所有 Pointer を返す */
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
 * @brief 診断可能な Move-only Error Value
 *
 * Primary Error、伝播 Context、Native 情報、再分類前の Cause を一つの所有 Value として失敗経路へ渡す
 * Immutable 参照は任意 Thread で利用できる
 * Mutation は外部同期し、Handler の Owner を Error より長く生存させる
 */
class Error final
{
  public:
    Error(const Error &) = delete;
    Error &operator=(const Error &) = delete;
    Error(Error &&) noexcept = default;
    Error &operator=(Error &&) noexcept = default;
    ~Error() = default;

    /** @brief Native Error なしの Error を生成する */
    [[nodiscard]] static Error create(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                      std::string_view a_summary) noexcept;

    /** @brief Native Error 付き Error を生成する */
    [[nodiscard]] static Error create(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                      std::string_view a_summary, NativeError &&a_nativeError) noexcept;

    /**
     * @brief Error を新しい抽象 Level へ再分類する
     * @param a_cause 消費される直前の Error
     *
     * 下位 Module の詳細を Cause へ保存しながら、呼び出し側が扱える上位 Domain の Error へ変換する
     */
    [[nodiscard]] static Error reclassify(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                          std::string_view a_summary, Error &&a_cause) noexcept;

    /**
     * @brief Native Error 付きで Error を新しい抽象 Level へ再分類する
     * @param a_cause 消費される直前の Error
     *
     * 新しい分類自身にも Native 情報が必要な場合に、Cause の Native 情報とは別に保持する
     */
    [[nodiscard]] static Error reclassify(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                          std::string_view a_summary, NativeError &&a_nativeError,
                                          Error &&a_cause) noexcept;

    /**
     * @brief 伝播 Context を追加する
     * @param a_emergencyHandler Allocation 失敗時の非所有終了境界
     * @param a_message Context Message
     * @param a_location Context 追加位置
     */
    void add_context(EmergencyHandler &a_emergencyHandler, std::string_view a_message,
                     std::source_location a_location = std::source_location::current()) noexcept;

    /** @brief Primary Error Code を返す */
    [[nodiscard]] const ErrorCode &code() const noexcept;

    /** @brief 開発者向け Summary を返す */
    [[nodiscard]] std::string_view summary() const noexcept;

    /** @brief 伝播 Context を追加順で返す */
    [[nodiscard]] std::span<const ErrorContext> contexts() const noexcept;

    /** @brief Native Error があれば非所有 Pointer を返す */
    [[nodiscard]] const NativeError *try_native_error() const noexcept;

    /** @brief 診断表示が失敗の伝播を追えるよう Immediate Cause から Root Cause までを順に返す */
    [[nodiscard]] std::span<const ErrorCause> causes() const noexcept;

    /** @brief Root Cause Code を返す */
    [[nodiscard]] const ErrorCode &root_code() const noexcept;

  private:
    [[nodiscard]] static Error reclassify_impl(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                               std::string_view a_summary, std::optional<NativeError> &&a_nativeError,
                                               Error &&a_cause) noexcept;

    Error(ErrorCode &&a_code, std::string &&a_summary, std::optional<NativeError> &&a_nativeError) noexcept;

    ErrorCode m_code;
    std::string m_summary;
    std::vector<ErrorContext> m_contexts;
    std::optional<NativeError> m_nativeError;
    std::vector<ErrorCause> m_causes;
};
} // namespace cue
