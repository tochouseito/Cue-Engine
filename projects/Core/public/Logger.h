#pragma once
#include <format>
#include <source_location>

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

        }
    private:
    };
}
