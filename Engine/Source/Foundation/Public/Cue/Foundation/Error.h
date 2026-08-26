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
class AssertContext;

/// @brief Domain 内で一意な Error 識別子
///
/// Domain と数値を分離し、Module 間で同じ数値を使用しても Error の意味が衝突しないようにする
/// Move-only Value として呼び出し側が所有する
class ErrorCode final
{
  public:
    /// @brief ErrorCode の一意所有を保つため Copy 構築を禁止する
    ErrorCode(const ErrorCode &) = delete;
    /// @brief ErrorCode の一意所有を保つため Copy 代入を禁止する
    ErrorCode &operator=(const ErrorCode &) = delete;
    /// @brief ErrorCode の状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    ErrorCode(ErrorCode &&) noexcept = default;
    /// @brief ErrorCode の状態を Move 代入し、移動元は有効だが内容未規定の状態にする
    ErrorCode &operator=(ErrorCode &&) noexcept = default;
    /// @brief ErrorCode が保持する Resource を所有権規則に従って破棄する
    ~ErrorCode() = default;

    /// @brief Error Code を生成する
    /// @param a_emergencyHandler Allocation 失敗時の非所有終了境界
    /// @param a_domain Code Domain
    /// @param a_value Domain 内の Code
    [[nodiscard]] static ErrorCode create(EmergencyHandler &a_emergencyHandler, std::string_view a_domain,
                                          std::int64_t a_value) noexcept;

    /// @brief Code Domain を返す
    [[nodiscard]] std::string_view domain() const noexcept;

    /// @brief Domain 内の Code を返す
    [[nodiscard]] std::int64_t value() const noexcept;

  private:
    /// @brief ErrorCode を必要な依存と初期状態から構築する
    ErrorCode(std::string &&a_domain, std::int64_t a_value) noexcept;

    std::string m_domain;
    std::int64_t m_value;
};

/// @brief Native Header へ依存しない Native Error 情報
///
/// Win32 や Graphics API の型を Foundation 公開 API へ漏らさず、元の低 Level 診断値を保持する
class NativeError final
{
  public:
    /// @brief NativeError の一意所有を保つため Copy 構築を禁止する
    NativeError(const NativeError &) = delete;
    /// @brief NativeError の一意所有を保つため Copy 代入を禁止する
    NativeError &operator=(const NativeError &) = delete;
    /// @brief NativeError の状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    NativeError(NativeError &&) noexcept = default;
    /// @brief NativeError の状態を Move 代入し、移動元は有効だが内容未規定の状態にする
    NativeError &operator=(NativeError &&) noexcept = default;
    /// @brief NativeError が保持する Resource を所有権規則に従って破棄する
    ~NativeError() = default;

    /// @brief Native Error 情報を生成する
    [[nodiscard]] static NativeError create(EmergencyHandler &a_emergencyHandler, std::string_view a_domain,
                                            std::int64_t a_value) noexcept;

    /// @brief Native Domain を返す
    [[nodiscard]] std::string_view domain() const noexcept;

    /// @brief Native Code を返す
    [[nodiscard]] std::int64_t value() const noexcept;

  private:
    /// @brief NativeError を必要な依存と初期状態から構築する
    NativeError(std::string &&a_domain, std::int64_t a_value) noexcept;

    std::string m_domain;
    std::int64_t m_value;
};

/// @brief Error 伝播時に追加された診断 Context
///
/// Error の分類を変えずに、失敗が通過した処理と Source 位置を積み重ねる
class ErrorContext final
{
  public:
    /// @brief ErrorContext の一意所有を保つため Copy 構築を禁止する
    ErrorContext(const ErrorContext &) = delete;
    /// @brief ErrorContext の一意所有を保つため Copy 代入を禁止する
    ErrorContext &operator=(const ErrorContext &) = delete;
    /// @brief ErrorContext の状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    ErrorContext(ErrorContext &&) noexcept = default;
    /// @brief ErrorContext の状態を Move 代入し、移動元は有効だが内容未規定の状態にする
    ErrorContext &operator=(ErrorContext &&) noexcept = default;
    /// @brief ErrorContext が保持する Resource を所有権規則に従って破棄する
    ~ErrorContext() = default;

    /// @brief Context Message を返す
    [[nodiscard]] std::string_view message() const noexcept;

    /// @brief Context 追加位置を返す
    [[nodiscard]] const SourceLocation &location() const noexcept;

  private:
    friend class Error;

    /// @brief ErrorContext を必要な依存と初期状態から構築する
    ErrorContext(std::string &&a_message, SourceLocation a_location) noexcept;

    std::string m_message;
    SourceLocation m_location;
};

/// @brief 再分類前の Error を保持する非再帰 Cause Frame
///
/// Error を再帰所有せず平坦な配列へ格納し、Cause Chain の寿命と走査順を単純に保つ
class ErrorCause final
{
  public:
    /// @brief ErrorCause の一意所有を保つため Copy 構築を禁止する
    ErrorCause(const ErrorCause &) = delete;
    /// @brief ErrorCause の一意所有を保つため Copy 代入を禁止する
    ErrorCause &operator=(const ErrorCause &) = delete;
    /// @brief ErrorCause の状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    ErrorCause(ErrorCause &&) noexcept = default;
    /// @brief ErrorCause の状態を Move 代入し、移動元は有効だが内容未規定の状態にする
    ErrorCause &operator=(ErrorCause &&) noexcept = default;
    /// @brief ErrorCause が保持する Resource を所有権規則に従って破棄する
    ~ErrorCause() = default;

    /// @brief 再分類前の Error Code を返す
    [[nodiscard]] const ErrorCode &code() const noexcept;

    /// @brief 再分類前の Summary を返す
    [[nodiscard]] std::string_view summary() const noexcept;

    /// @brief 再分類前の Context を返す
    [[nodiscard]] std::span<const ErrorContext> contexts() const noexcept;

    /// @brief Native Error があれば非所有 Pointer を返す
    [[nodiscard]] const NativeError *try_native_error() const noexcept;

  private:
    friend class Error;

    /// @brief ErrorCause を必要な依存と初期状態から構築する
    ErrorCause(ErrorCode &&a_code, std::string &&a_summary, std::vector<ErrorContext> &&a_contexts,
               std::optional<NativeError> &&a_nativeError) noexcept;

    ErrorCode m_code;
    std::string m_summary;
    std::vector<ErrorContext> m_contexts;
    std::optional<NativeError> m_nativeError;
};

/// @brief 診断可能な Move-only Error Value
///
/// Primary Error、伝播 Context、Native 情報、再分類前の Cause を一つの所有 Value として失敗経路へ渡す
/// Immutable 参照は任意 Thread で利用できる
/// Mutation は外部同期し、Handler の Owner を Error より長く生存させる
class Error final
{
  public:
    /// @brief Error の一意所有を保つため Copy 構築を禁止する
    Error(const Error &) = delete;
    /// @brief Error の一意所有を保つため Copy 代入を禁止する
    Error &operator=(const Error &) = delete;
    /// @brief Error の状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    Error(Error &&) noexcept = default;
    /// @brief Error の状態を Move 代入し、移動元は有効だが内容未規定の状態にする
    Error &operator=(Error &&) noexcept = default;
    /// @brief Error が保持する Resource を所有権規則に従って破棄する
    ~Error() = default;

    /// @brief Native Error なしの Error を生成する
    [[nodiscard]] static Error create(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                      std::string_view a_summary) noexcept;

    /// @brief Native Error 付き Error を生成する
    [[nodiscard]] static Error create(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                      std::string_view a_summary, NativeError &&a_nativeError) noexcept;

    /// @brief Error を新しい抽象 Level へ再分類する
    /// @param a_cause 消費される直前の Error
    ///
    /// 下位 Module の詳細を Cause へ保存しながら、呼び出し側が扱える上位 Domain の Error へ変換する
    [[nodiscard]] static Error reclassify(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                          std::string_view a_summary, Error &&a_cause) noexcept;

    /// @brief Native Error 付きで Error を新しい抽象 Level へ再分類する
    /// @param a_cause 消費される直前の Error
    ///
    /// 新しい分類自身にも Native 情報が必要な場合に、Cause の Native 情報とは別に保持する
    [[nodiscard]] static Error reclassify(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                          std::string_view a_summary, NativeError &&a_nativeError,
                                          Error &&a_cause) noexcept;

    /// @brief 伝播 Context を追加する
    /// @param a_emergencyHandler Allocation 失敗時の非所有終了境界
    /// @param a_message Context Message
    /// @param a_location Context 追加位置
    void add_context(EmergencyHandler &a_emergencyHandler, std::string_view a_message,
                     std::source_location a_location = std::source_location::current()) noexcept;

    /// @brief Secondary Error の診断を Primary Error の Context へ転記する
    /// @param a_assertContext 自己参照違反の診断と Allocation 失敗時の非所有終了境界
    /// @param a_secondaryError 呼び出し中だけ参照する Secondary Error
    /// @param a_context Secondary Error が発生した後続処理の説明
    /// @param a_label Code と Native Error を識別する診断 Prefix
    /// @param a_location 新しく生成する Context へ記録する実際の呼び出し位置
    /// @pre a_secondaryError は呼び出し対象の Primary Error と異なる Object であること
    void append_secondary_diagnostics(
        const AssertContext &a_assertContext, const Error &a_secondaryError,
        std::string_view a_context, std::string_view a_label,
        std::source_location a_location = std::source_location::current()) noexcept;

    /// @brief Secondary Cause の診断を Primary Error の Context へ転記する
    /// @param a_emergencyHandler Allocation 失敗時の非所有終了境界
    /// @param a_secondaryCause 呼び出し中だけ参照する Secondary Cause
    /// @param a_context Secondary Cause が発生した後続処理の説明
    /// @param a_label Code と Native Error を識別する診断 Prefix
    /// @param a_location 新しく生成する Context へ記録する実際の呼び出し位置
    void append_secondary_diagnostics(
        EmergencyHandler &a_emergencyHandler, const ErrorCause &a_secondaryCause,
        std::string_view a_context, std::string_view a_label,
        std::source_location a_location = std::source_location::current()) noexcept;

    /// @brief Primary Error Code を返す
    [[nodiscard]] const ErrorCode &code() const noexcept;

    /// @brief 開発者向け Summary を返す
    [[nodiscard]] std::string_view summary() const noexcept;

    /// @brief 伝播 Context を追加順で返す
    [[nodiscard]] std::span<const ErrorContext> contexts() const noexcept;

    /// @brief Native Error があれば非所有 Pointer を返す
    [[nodiscard]] const NativeError *try_native_error() const noexcept;

    /// @brief 診断表示が失敗の伝播を追えるよう Immediate Cause から Root Cause までを順に返す
    [[nodiscard]] std::span<const ErrorCause> causes() const noexcept;

    /// @brief Root Cause Code を返す
    [[nodiscard]] const ErrorCode &root_code() const noexcept;

  private:
    /// @brief Foundation Error 診断の Impl を既存の診断情報を失わず追加または再分類する
    [[nodiscard]] static Error reclassify_impl(EmergencyHandler &a_emergencyHandler, ErrorCode &&a_code,
                                               std::string_view a_summary, std::optional<NativeError> &&a_nativeError,
                                               Error &&a_cause) noexcept;

    /// @brief 転記元 Error Context の Message と SourceLocation を変更せず追加する
    void append_preserved_context(EmergencyHandler &a_emergencyHandler,
                                  const ErrorContext &a_context) noexcept;

    /// @brief Error を必要な依存と初期状態から構築する
    Error(ErrorCode &&a_code, std::string &&a_summary, std::optional<NativeError> &&a_nativeError) noexcept;

    ErrorCode m_code;
    std::string m_summary;
    std::vector<ErrorContext> m_contexts;
    std::optional<NativeError> m_nativeError;
    std::vector<ErrorCause> m_causes;
};
} // namespace cue
