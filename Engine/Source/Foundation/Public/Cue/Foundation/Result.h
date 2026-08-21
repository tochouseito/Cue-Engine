#pragma once

#include <Cue/Foundation/Error.h>

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace cue
{
/**
 * @brief 成功ValueまたはErrorの正確に一方を所有する
 * @tparam T `noexcept` Move可能な成功Value
 */
template <typename T> class Result final
{
    static_assert(std::is_object_v<T>);
    static_assert(!std::is_same_v<std::remove_cv_t<T>, Error>);
    static_assert(std::is_nothrow_move_constructible_v<T>);
    static_assert(std::is_nothrow_move_assignable_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);

  public:
    Result() = delete;
    Result(const Result &) = delete;
    Result &operator=(const Result &) = delete;
    Result(Result &&) noexcept = default;
    Result &operator=(Result &&) noexcept = default;
    ~Result() = default;

    /** @brief 成功Valueの所有権を受け取る */
    [[nodiscard]] static Result success(T &&a_value) noexcept
    {
        return Result(std::in_place_index<0>, std::move(a_value));
    }

    /** @brief Errorの所有権を受け取る */
    [[nodiscard]] static Result failure(Error &&a_error) noexcept
    {
        return Result(std::in_place_index<1>, std::move(a_error));
    }

    /** @brief 成功Valueを保持する場合にtrueを返す */
    [[nodiscard]] bool has_value() const noexcept
    {
        return m_storage.index() == 0;
    }

    /** @brief 成功Valueを保持する場合にtrueを返す */
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    /**
     * @brief 成功Valueへの非所有Pointerを返す
     * @return Error状態ではnullptr
     */
    [[nodiscard]] T *try_value() & noexcept
    {
        return std::get_if<0>(&m_storage);
    }

    /** @copydoc try_value() */
    [[nodiscard]] const T *try_value() const & noexcept
    {
        return std::get_if<0>(&m_storage);
    }

    T *try_value() && = delete;
    const T *try_value() const && = delete;

    /**
     * @brief Errorへの非所有Pointerを返す
     * @return 成功状態ではnullptr
     */
    [[nodiscard]] Error *try_error() & noexcept
    {
        return std::get_if<1>(&m_storage);
    }

    /** @copydoc try_error() */
    [[nodiscard]] const Error *try_error() const & noexcept
    {
        return std::get_if<1>(&m_storage);
    }

    Error *try_error() && = delete;
    const Error *try_error() const && = delete;

  private:
    template <std::size_t Index, typename Value>
    explicit Result(std::in_place_index_t<Index> a_index, Value &&a_value) noexcept
        : m_storage(a_index, std::forward<Value>(a_value))
    {
    }

    std::variant<T, Error> m_storage;
};

/**
 * @brief Valueを持たない成功またはErrorの正確に一方を所有する
 */
template <> class Result<void> final
{
  public:
    Result() = delete;
    Result(const Result &) = delete;
    Result &operator=(const Result &) = delete;
    Result(Result &&) noexcept = default;
    Result &operator=(Result &&) noexcept = default;
    ~Result() = default;

    /** @brief 成功状態を生成する */
    [[nodiscard]] static Result success() noexcept
    {
        return Result(SuccessTag{});
    }

    /** @brief Errorの所有権を受け取る */
    [[nodiscard]] static Result failure(Error &&a_error) noexcept
    {
        return Result(std::move(a_error));
    }

    /** @brief 成功状態の場合にtrueを返す */
    [[nodiscard]] bool has_value() const noexcept
    {
        return !m_error.has_value();
    }

    /** @brief 成功状態の場合にtrueを返す */
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    /**
     * @brief Errorへの非所有Pointerを返す
     * @return 成功状態ではnullptr
     */
    [[nodiscard]] Error *try_error() & noexcept
    {
        return m_error ? &m_error.value() : nullptr;
    }

    /** @copydoc try_error() */
    [[nodiscard]] const Error *try_error() const & noexcept
    {
        return m_error ? &m_error.value() : nullptr;
    }

    Error *try_error() && = delete;
    const Error *try_error() const && = delete;

  private:
    struct SuccessTag
    {
    };

    explicit Result(SuccessTag) noexcept
    {
    }

    explicit Result(Error &&a_error) noexcept : m_error(std::move(a_error))
    {
    }

    std::optional<Error> m_error;
};
} // namespace cue
