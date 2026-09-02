#pragma once

#include <Cue/Foundation/Error.h>

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace cue
{
/// @brief 成功 Value または Error の正確に一方を所有する
/// @tparam T `noexcept` Move構築可能な成功 Value
///
/// 回復可能な失敗を Exception や無効 Value へ変換せず、呼び出し側へ明示的に伝播するための戻り値型
template <typename T> class Result final
{
    // Error 伝播中に別の例外が発生して元の失敗情報を失わないよう、格納 Value には非例外 Move を要求する
    static_assert(std::is_object_v<T>);
    static_assert(!std::is_same_v<std::remove_cv_t<T>, Error>);
    static_assert(std::is_nothrow_move_constructible_v<T>);
    static_assert(!std::is_move_assignable_v<T> || std::is_nothrow_move_assignable_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);

  public:
    /// @brief 無効な Result 状態を作らせないため既定構築を禁止する
    Result() = delete;
    /// @brief Result の一意所有を保つため Copy 構築を禁止する
    Result(const Result &) = delete;
    /// @brief Result の一意所有を保つため Copy 代入を禁止する
    Result &operator=(const Result &) = delete;
    /// @brief Result の状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    Result(Result &&) noexcept = default;
    /// @brief 成功ValueがMove代入可能な場合にResultの状態をMove代入する
    ///
    /// 成功ValueがMove代入不可能な場合、この演算子はCompilerにより削除される
    Result &operator=(Result &&) noexcept = default;
    /// @brief Result が保持する Resource を所有権規則に従って破棄する
    ~Result() = default;

    /// @brief 成功 Value の所有権を受け取る
    [[nodiscard]] static Result success(T &&a_value) noexcept
    {
        return Result(std::in_place_index<0>, std::move(a_value));
    }

    /// @brief Error の所有権を受け取る
    [[nodiscard]] static Result failure(Error &&a_error) noexcept
    {
        return Result(std::in_place_index<1>, std::move(a_error));
    }

    /// @brief 成功 Value を保持する場合に true を返す
    [[nodiscard]] bool has_value() const noexcept
    {
        return m_storage.index() == 0;
    }

    /// @brief 成功 Value を保持する場合に true を返す
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    /// @brief 成功 Value への非所有 Pointer を返す
    /// @return Error 状態では nullptr
    [[nodiscard]] T *try_value() & noexcept
    {
        return std::get_if<0>(&m_storage);
    }

    /// @copydoc try_value()
    /// @brief 成功状態の Value へ非所有 Access を提供し、Error 状態では Null を返す
    [[nodiscard]] const T *try_value() const & noexcept
    {
        return std::get_if<0>(&m_storage);
    }

    /// @brief Temporary Result から寿命切れになる Value Pointer を取得させない
    T *try_value() && = delete;
    /// @brief Const Temporary Result から寿命切れになる Value Pointer を取得させない
    const T *try_value() const && = delete;

    /// @brief Error への非所有 Pointer を返す
    /// @return 成功状態では nullptr
    [[nodiscard]] Error *try_error() & noexcept
    {
        return std::get_if<1>(&m_storage);
    }

    /// @copydoc try_error()
    /// @brief Error 状態の Error へ非所有 Access を提供し、成功状態では Null を返す
    [[nodiscard]] const Error *try_error() const & noexcept
    {
        return std::get_if<1>(&m_storage);
    }

    /// @brief Temporary Result から寿命切れになる Error Pointer を取得させない
    Error *try_error() && = delete;
    /// @brief Const Temporary Result から寿命切れになる Error Pointer を取得させない
    const Error *try_error() const && = delete;

  private:
    /// @brief 成功 Value または Error の正確に一方を保持する Result 状態を構築する
    template <std::size_t Index, typename Value>
    explicit Result(std::in_place_index_t<Index> a_index, Value &&a_value) noexcept
        : m_storage(a_index, std::forward<Value>(a_value))
    {
    }

    std::variant<T, Error> m_storage;
};

/// @brief Value を持たない成功または Error の正確に一方を所有する
///
/// 成功時に返す Data がない操作でも、Result<T> と同じ明示的な失敗伝播を維持する
template <> class Result<void> final
{
  public:
    /// @brief 無効な Result 状態を作らせないため既定構築を禁止する
    Result() = delete;
    /// @brief Result の一意所有を保つため Copy 構築を禁止する
    Result(const Result &) = delete;
    /// @brief Result の一意所有を保つため Copy 代入を禁止する
    Result &operator=(const Result &) = delete;
    /// @brief Result の状態を Move 構築し、移動元は有効だが内容未規定の状態にする
    Result(Result &&) noexcept = default;
    /// @brief Result の状態を Move 代入し、移動元は有効だが内容未規定の状態にする
    Result &operator=(Result &&) noexcept = default;
    /// @brief Result が保持する Resource を所有権規則に従って破棄する
    ~Result() = default;

    /// @brief 成功状態を生成する
    [[nodiscard]] static Result success() noexcept
    {
        return Result(SuccessTag{});
    }

    /// @brief Error の所有権を受け取る
    [[nodiscard]] static Result failure(Error &&a_error) noexcept
    {
        return Result(std::move(a_error));
    }

    /// @brief 成功状態の場合に true を返す
    [[nodiscard]] bool has_value() const noexcept
    {
        return !m_error.has_value();
    }

    /// @brief 成功状態の場合に true を返す
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    /// @brief Error への非所有 Pointer を返す
    /// @return 成功状態では nullptr
    [[nodiscard]] Error *try_error() & noexcept
    {
        return m_error ? &m_error.value() : nullptr;
    }

    /// @copydoc try_error()
    /// @brief Error 状態の Error へ非所有 Access を提供し、成功状態では Null を返す
    [[nodiscard]] const Error *try_error() const & noexcept
    {
        return m_error ? &m_error.value() : nullptr;
    }

    /// @brief Temporary Result から寿命切れになる Error Pointer を取得させない
    Error *try_error() && = delete;
    /// @brief Const Temporary Result から寿命切れになる Error Pointer を取得させない
    const Error *try_error() const && = delete;

  private:
    struct SuccessTag
    {
    };

    /// @brief Error を持たない空の成功状態として Result<void> を構築する
    explicit Result(SuccessTag) noexcept
    {
    }

    /// @brief Error の所有権を保持する失敗状態として Result<void> を構築する
    explicit Result(Error &&a_error) noexcept : m_error(std::move(a_error))
    {
    }

    std::optional<Error> m_error;
};
} // namespace cue
