// === Base includes ===
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
    constexpr size_t k_messageBufferSize = 2048;

    static void format_user_message(
        char* a_destination,
        size_t a_destinationSize,
        const char* a_format,
        va_list a_args) noexcept
    {
        // 1) 出力先を検証する
        if (a_destination == nullptr || a_destinationSize == 0)
        {
            return;
        }

        // 2) フォーマットが無ければ空文字にする
        if (a_format == nullptr)
        {
            a_destination[0] = '\0';
            return;
        }

        // 3) 可変長引数を安全側で整形する
        const int result = std::vsnprintf(a_destination, a_destinationSize, a_format, a_args);
        if (result < 0)
        {
            a_destination[0] = '\0';
            return;
        }

        // 4) 念のため終端を保証する
        a_destination[a_destinationSize - 1] = '\0';
    }
}

namespace Cue
{
    [[noreturn]] void assert_fail([[maybe_unused]] const AssertContext& a_context) noexcept
    {
        // 1) 表示文の組み立て
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

        // 2) デバッガ出力
        ::OutputDebugStringA(messageBuffer);

        // 3) メッセージボックスの表示
        const int result = MessageBoxA(
            nullptr,
            messageBuffer,
            "Cue Assert Failed!",
            MB_ABORTRETRYIGNORE | MB_ICONERROR);

        // 4) ユーザーの選択に応じた処理
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

        // 4) 強制終了
        std::abort();
    }

    [[noreturn]] void assert_fail_format(
        const char* a_expression,
        const std::source_location& a_location,
        const char* a_format,
        ...) noexcept
    {
        // 1) フォーマットされたメッセージの作成
        char message[k_messageBufferSize];

        va_list args;
        va_start(args, a_format);
        format_user_message(message, sizeof(message), a_format, args);
        va_end(args);

        // 2) アサート失敗のコンテキストを作成する
        const AssertContext context = make_assert_context(a_expression, message, a_location);

        // 3) アサート失敗の処理関数を呼び出す
        assert_fail(context);
    }
}

#endif // CUE_DEBUG
