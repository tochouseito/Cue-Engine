#pragma once

/// *********************************************************************************
/// アサーション
/// *********************************************************************************

// === C++ includes ===
#include <cstdlib>

#ifdef CUE_DEBUG
// === C++ includes ===
#include <cstdint>
#include <source_location>

namespace Cue
{
    /// @brief アサート失敗時に記録するコンテキスト
    struct AssertContext final
    {
        const char* expression = ""; // アサート式
        const char* message = "";    // メッセージ
        const char* file = "";       // ファイル名
        const char* function = "";   // 関数名
        uint_least32_t line = 0;     // 行番号
    };

    /// @brief アサート失敗コンテキストを構築
    /// @param a_expression 失敗した式文字列
    /// @param a_message 追加メッセージ
    /// @param a_location 呼び出し位置
    /// @return 構築したコンテキスト
    [[nodiscard]] inline AssertContext make_assert_context(
        const char* a_expression,
        const char* a_message = "",
        const std::source_location& a_location = std::source_location::current()) noexcept
    {
        if (a_expression == nullptr)
        {
            a_expression = "";
        }

        if (a_message == nullptr)
        {
            a_message = "";
        }

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

    /// @brief 整形メッセージ付きでアサート失敗処理を実行
    /// @param a_expression 失敗した式文字列
    /// @param a_location 呼び出し位置
    /// @param a_format 整形文字列
    [[noreturn]] void assert_fail_format(
        const char* a_expression,
        const std::source_location& a_location,
        const char* a_format,
        ...) noexcept;
} // namespace Cue

/// @brief アサートマクロ(式のみ)
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

/// @brief アサートマクロ(式 + メッセージ)
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

/// @brief アサートマクロ(式 + 整形メッセージ)
#define CUE_ASSERT_FORMAT(expr, format, ...)                                       \
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

/// リリースビルドではアサートを無効化
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
#define CUE_ASSERT_FORMAT(expr, format, ...) \
        do                                                                       \
        {                                                                        \
            if (!(expr))                                                          \
            {                                                                    \
                std::abort();                                                    \
            }                                                                    \
        }                                                                        \
        while (false)

#endif // CUE_DEBUG
