#pragma once
#include <format>
#include <source_location>
#include <type_traits>
#include <string_view>
#ifdef CUE_DEBUG
#include <Windows.h>
#endif

namespace Cue::Core
{
    enum class LogSink : uint8_t
    {
        none = 0,
        debugConsole = 1u << 0,
        file = 1u << 1,
    };

    // 1) enum class 用のビット演算子
    constexpr LogSink operator|(LogSink a, LogSink b) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return static_cast<LogSink>(static_cast<U>(a) | static_cast<U>(b));
    }

    constexpr LogSink operator&(LogSink a, LogSink b) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return static_cast<LogSink>(static_cast<U>(a) & static_cast<U>(b));
    }

    constexpr LogSink& operator|=(LogSink& a, LogSink b) noexcept
    {
        a = (a | b);
        return a;
    }

    constexpr bool has_sink(LogSink mask, LogSink bit) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return (static_cast<U>(mask & bit) != 0);
    }

    class Logger
    {
    public:
        // ログ出力
        template <typename... Args>
        static void log(LogSink sink, std::string_view fmt, Args&&... args)
        {
            std::string message = std::vformat(fmt, std::make_format_args(args...));
            // 2) 末尾改行を保証（既に改行があるなら足さない）
            if (message.empty() || message.back() != '\n')
            {
                message += "\n"; // Windowsでも大抵これでOK（気になるなら "\r\n"）
            }
            if (has_sink(sink, LogSink::debugConsole))
            {
#ifdef CUE_DEBUG
                // デバッグコンソールに出力
                ::OutputDebugStringA(message.c_str());
#endif
            }
        }
    private:
    };
}
