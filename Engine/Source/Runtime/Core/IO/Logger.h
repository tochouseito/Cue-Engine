#pragma once

// === C++ includes ===
#include <format>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace Cue::Core::IO
{
    enum class LogSink : uint8_t
    {
        none = 0,
        debugConsole = 1u << 0,
        file = 1u << 1,
    };

    // 1) enum class 用のビット演算子
    constexpr LogSink operator|(LogSink a_left, LogSink a_right) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return static_cast<LogSink>(static_cast<U>(a_left) | static_cast<U>(a_right));
    }

    constexpr LogSink operator&(LogSink a_left, LogSink a_right) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return static_cast<LogSink>(static_cast<U>(a_left) & static_cast<U>(a_right));
    }

    constexpr LogSink& operator|=(LogSink& a_left, LogSink a_right) noexcept
    {
        a_left = (a_left | a_right);
        return a_left;
    }

    constexpr bool has_sink(LogSink a_mask, LogSink a_bit) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return (static_cast<U>(a_mask & a_bit) != 0);
    }

    /// @brief デバッグコンソールへ文字列を出力します。
    void out_debug_console(std::string_view a_message);

    /// @brief 指定 sink へ整形済みログを出力します。
    template <typename... Args>
    void log(LogSink a_sink, std::string_view a_format, Args&&... a_args)
    {
        std::string message = std::vformat(a_format, std::make_format_args(a_args...));
        // 2) 末尾改行を保証（既に改行があるなら足さない）
        if (message.empty() || message.back() != '\n')
        {
            message += "\n"; // Windowsでも大抵これでOK（気になるなら "\r\n"）
        }
        if (has_sink(a_sink, LogSink::debugConsole))
        {
            // デバッグコンソールに出力
            out_debug_console(message);
        }
    }
}
