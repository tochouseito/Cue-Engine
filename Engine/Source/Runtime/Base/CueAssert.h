// CueAssert の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstdlib>

#ifdef CUE_DEBUG
// === C++ includes ===
#include <cstdint>
#include <source_location>

/*
%d → int

%u → unsigned int

%f → float / double

%c → 1文字

%s → 文字列（C文字列）

%x → 16進数

%p → ポインタ
*/

namespace Cue
{
    /// @brief アサート失敗時に記録するコンテキスト
    struct AssertContext final
    {
        const char* expression = "";
        const char* message = "";
        const char* file = "";
        const char* function = "";
        uint_least32_t line = 0;
    };

    /// @brief アサート失敗コンテキストを構築する
    /// @param a_expression 失敗した式文字列
    /// @param a_message 追加メッセージ
    /// @param a_location 呼び出し位置
    /// @return 構築したコンテキスト
    [[nodiscard]] inline AssertContext make_assert_context(
        const char* a_expression,
        const char* a_message = "",
        const std::source_location& a_location = std::source_location::current()) noexcept
    {
        // - null を空文字へ正規化
        if (a_expression == nullptr)
        {
            a_expression = "";
        }

        if (a_message == nullptr)
        {
            a_message = "";
        }

        // - 失敗情報を組み立てる
        return AssertContext{
            a_expression,
            a_message,
            a_location.file_name(),
            a_location.function_name(),
            a_location.line()
        };
    }

    /// @brief アサート失敗時の共通処理
    /// @param a_context 失敗コンテキスト
    [[noreturn]] void assert_fail([[maybe_unused]] const AssertContext& a_context) noexcept;

    /// @brief 整形メッセージ付きでアサート失敗処理を行いる
    /// @param a_expression 失敗した式文字列
    /// @param a_location 呼び出し位置
    /// @param a_format 整形文字列
    [[noreturn]] void assert_fail_format(
        const char* a_expression,
        const std::source_location& a_location,
        const char* a_format,
        ...) noexcept;
} // namespace Cue

#define CUE_ASSERT(expr)                                                     \
        do                                                                       \
        {                                                                        \
            if (!(expr))                                                         \
            {                                                                    \
                Cue::assert_fail(                                        \
                    Cue::make_assert_context(#expr));                    \
            }                                                                    \
        }                                                                        \
        while (false)

#define CUE_ASSERT_MSG(expr, message)                                        \
        do                                                                       \
        {                                                                        \
            if (!(expr))                                                         \
            {                                                                    \
                Cue::assert_fail(                                        \
                    Cue::make_assert_context(#expr, (message)));         \
            }                                                                    \
        }                                                                        \
        while (false)

#define CUE_ASSERTF(expr, format, ...)                                       \
        do                                                                       \
        {                                                                        \
            if (!(expr))                                                         \
            {                                                                    \
                Cue::assert_fail_format(                                 \
                    #expr,                                                       \
                    std::source_location::current(),                             \
                    (format) __VA_OPT__(,) __VA_ARGS__);                         \
            }                                                                    \
        }                                                                        \
        while (false)

#else

#define CUE_ASSERT(expr) \
        do                                                                       \
        {                                                                        \
            if (!(expr))                                                          \
            {                                                                    \
                std::abort();                                                    \
            }                                                                    \
        }                                                                        \
        while (false)
#define CUE_ASSERT_MSG(expr, message) \
        do                                                                       \
        {                                                                        \
            if (!(expr))                                                          \
            {                                                                    \
                std::abort();                                                    \
            }                                                                    \
        }                                                                        \
        while (false)
#define CUE_ASSERTF(expr, format, ...) \
        do                                                                       \
        {                                                                        \
            if (!(expr))                                                          \
            {                                                                    \
                std::abort();                                                    \
            }                                                                    \
        }                                                                        \
        while (false)

#endif // CUE_DEBUG
