#pragma once

#ifdef CUE_DEBUG
// C++ includes
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
    // アサート失敗のコンテキスト情報
    struct AssertContext final
    {
        const char* expression = "";
        const char* message = "";
        const char* file = "";
        const char* function = "";
        uint_least32_t line = 0;
    };

    // アサートコンテキストを作成するユーティリティ関数
    [[nodiscard]] inline AssertContext make_assert_context(
        const char* expression,
        const char* message = "",
        const std::source_location& location = std::source_location::current()) noexcept
    {
        // 1) null を空文字へ正規化
        if (expression == nullptr)
        {
            expression = "";
        }

        if (message == nullptr)
        {
            message = "";
        }

        // 2) 失敗情報を組み立てる
        return AssertContext{
            expression,
            message,
            location.file_name(),
            location.function_name(),
            location.line()
        };
    }

    // アサート失敗の処理関数
    [[noreturn]] void assert_fail([[maybe_unused]] const AssertContext& context) noexcept;

    // フォーマット付きアサート失敗の処理関数
    [[noreturn]] void assert_fail_format(
        const char* expression,
        const std::source_location& location,
        const char* format,
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
            if(!(expr))                                                       \
            {                                                                    \
                std::abort();                                                    \
            }                                                                    \
        }                                                                        \
        while (false)
#define CUE_ASSERT_MSG(expr, message) \
        do                                                                       \
        {                                                                        \
            if(!(expr))                                                       \
            {                                                                    \
                std::abort();                                                    \
            }                                                                    \
        }                                                                        \
        while (false)
#define CUE_ASSERTF(expr, format, ...) \
        do                                                                       \
        {                                                                        \
            if(!(expr))                                                       \
            {                                                                    \
                std::abort();                                                    \
            }                                                                    \
        }                                                                        \
        while (false)

#endif // CUE_DEBUG
