#include "CueAssert.h"

#ifdef CUE_DEBUG

// === C++ includes ===
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

// === Windows API includes ===
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <intrin.h>

namespace
{
    /// @brief アサートメッセージのバッファサイズ
    constexpr size_t k_messageBufferSize = 1024;
}

namespace Cue
{
    [[noreturn]] void assert_fail([[maybe_unused]] const AssertContext& a_context) noexcept
    {
        // 表示文の組み立て
        char messageBuffer[k_messageBufferSize];
        std::snprintf(
            messageBuffer,
            sizeof(messageBuffer),
            "Assertion failed!\n\n"
            "Expression : %s\n"
            "Message    : %s\n"
            "File       : %s\n"
            "Line       : %d\n"
            "Function   : %s\n\n"
            "Abort  = terminate process\n"
            "Retry  = break into debugger\n"
            "Ignore = continue execution",
            a_context.expression,
            a_context.message,
            a_context.file,
            a_context.line,
            a_context.function);

        // デバッガ出力
        ::OutputDebugStringA(messageBuffer);

        // メッセージボックスの表示
        const int result = MessageBoxA(
            nullptr,
            messageBuffer,
            "Cue Assert Failed!",
            MB_ABORTRETRYIGNORE | MB_ICONERROR);

        // ユーザーの選択に応じた処理
        if (result == IDRETRY)
        {
            if (::IsDebuggerPresent() != false)
            {
                __debugbreak();
            }
        }

        if (result == IDIGNORE)
        {
        }

        // 強制終了
        std::abort();
    }

    [[noreturn]] void assert_fail_format(
        const char* a_expression,
        const std::source_location& a_location,
        const char* a_format,
        ...) noexcept
    {
        // フォーマットされたメッセージの作成
        char message[k_messageBufferSize];

        va_list args;
        va_start(args, a_format);

        // フォーマットが無ければ空文字にする
        if (a_format == nullptr)
        {
            message[0] = '\0';
        }

        // 可変長引数を整形する
        const int result = std::vsnprintf(message, sizeof(message), a_format, args);
        if (result < 0)
        {
            message[0] = '\0';
        }

        // 終端の保証
        message[sizeof(message) - 1] = '\0';

        va_end(args);

        // アサート失敗のコンテキストを作成する
        const AssertContext context = make_assert_context(a_expression, message, a_location);

        // アサート失敗の処理関数を呼び出す
        assert_fail(context);
    }
}

#endif // CUE_DEBUG
