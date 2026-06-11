#pragma once

/// *********************************************************************************
/// ログ出力
/// *********************************************************************************

// === C++ includes ===
#include <cstdint>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

// === Core includes ===
#include "IFileSystem.h"

namespace Cue::Core::IO
{
    /// @brief ログ出力先
    enum class LogSink : uint8_t
    {
        console = 1u << 0, // デバッグコンソール
        file = 1u << 1, // ファイル
    };

    /// @brief LogSink の OR 演算
    /// @param a_left 左辺
    /// @param a_right 右辺
    /// @return OR 結果
    constexpr LogSink operator|(LogSink a_left, LogSink a_right) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return static_cast<LogSink>(static_cast<U>(a_left) | static_cast<U>(a_right));
    }

    /// @brief LogSink の AND 演算
    /// @param a_left 左辺
    /// @param a_right 右辺
    /// @return AND 結果
    constexpr LogSink operator&(LogSink a_left, LogSink a_right) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return static_cast<LogSink>(static_cast<U>(a_left) & static_cast<U>(a_right));
    }

    /// @brief LogSink の OR 代入演算
    /// @param a_left 左辺
    /// @param a_right 右辺
    /// @return 更新後の左辺
    constexpr LogSink& operator|=(LogSink& a_left, LogSink a_right) noexcept
    {
        a_left = (a_left | a_right);
        return a_left;
    }

    /// @brief 指定 sink が含まれているかを返す
    /// @param a_mask 検査対象の sink マスク
    /// @param a_bit 検査する sink
    /// @return 含まれている場合は true
    constexpr bool has_sink(LogSink a_mask, LogSink a_bit) noexcept
    {
        using U = std::underlying_type_t<LogSink>;
        return (static_cast<U>(a_mask & a_bit) != 0);
    }

    /// @brief デバッグコンソールへ文字列を出力
    void out_debug_console(std::string_view a_message);

    /// @brief ファイルログの出力先を設定
    /// @param a_fileSystem ファイル書き込みに使用するファイルシステム
    /// @param a_path ログ出力先パス
    /// @param a_truncate 既存ファイルを切り詰める場合は true
    /// @return 設定結果
    Result set_log_file(IFileSystem& a_fileSystem, Path a_path, bool a_truncate = false) noexcept;

    /// @brief ファイルログの出力先を解除
    void clear_log_file() noexcept;

    /// @brief ファイルログへ文字列を出力
    void out_log_file(std::string_view a_message) noexcept;

    /// @brief 指定 sink へ整形済みログを出力
    template <typename... Args>
    void log(LogSink a_sink, std::string_view a_format, Args&&... a_args)
    {
        a_sink |= LogSink::file;

        std::string message = std::vformat(a_format, std::make_format_args(a_args...));
        // 末尾改行を保証
        if (message.empty() || message.back() != '\n')
        {
            message += "\n";
        }

        if (has_sink(a_sink, LogSink::console))
        {
            out_debug_console(message);
        }
        if (has_sink(a_sink, LogSink::file))
        {
            out_log_file(message);
        }
    }
}
