#include "CueAssert.h"

#ifdef CUE_DEBUG

// C++ includes
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Windows API
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
    constexpr size_t k_message_buffer_size = 2048;

    static void format_user_message(
        char* dst,
        size_t dstSize,
        const char* format,
        va_list args) noexcept
    {
        // 1) 出力先を検証する
        if (dst == nullptr || dstSize == 0)
        {
            return;
        }

        // 2) format が無ければ空文字にする
        if (format == nullptr)
        {
            dst[0] = '\0';
            return;
        }

        // 3) 可変長引数を安全側で整形する
        const int result = std::vsnprintf(dst, dstSize, format, args);
        if (result < 0)
        {
            dst[0] = '\0';
            return;
        }

        // 4) 念のため終端を保証する
        dst[dstSize - 1] = '\0';
    }
}

namespace Cue
{
    [[noreturn]] void assert_fail([[maybe_unused]] const AssertContext& context) noexcept
    {
        // 1) 表示文の組み立て
        char buffer[k_message_buffer_size];
        std::snprintf(
            buffer,
            sizeof(buffer),
            "Assertion failed!\n\n"
            "Expression : %s\n"
            "Message    : %s\n"
            "File       : %s\n"
            "Line       : %d\n"
            "Function   : %s\n\n"
            "Abort  = terminate process\n"
            "Retry  = break into debugger\n"
            "Ignore = continue execution",
            context.expression,
            context.message,
            context.file,
            context.line,
            context.function);

        // 2) デバッガ出力
        ::OutputDebugStringA(buffer);

        // 3) メッセージボックスの表示
        const int result = MessageBoxA(
            nullptr,
            buffer,
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
        const char* expression,
        const std::source_location& location,
        const char* format,
        ...) noexcept
    {
        // 1) フォーマットされたメッセージの作成
        char message[k_message_buffer_size];

        va_list args;
        va_start(args, format);
        format_user_message(message, sizeof(message), format, args);
        va_end(args);

        // 2) アサート失敗のコンテキストを作成する
        const AssertContext context = make_assert_context(expression, message, location);

        // 3) アサート失敗の処理関数を呼び出す
        assert_fail(context);
    }
}

#endif // CUE_DEBUG
