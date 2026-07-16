#pragma once

///********************************************************************************
/// エンジン内部の処理結果コード
///********************************************************************************

// === C++ includes ===
#include <cstdint>
#include <source_location>
#include <string_view>

namespace Cue
{
    /// @brief 処理結果種別コード
    enum class Code : uint16_t
    {
        OK = 0,             // 成功
        InvalidArgument,    // 無効な引数
        NotFound,           // 見つからない
        Unsupported,        // 非対応
        OutOfMemory,        // メモリ不足
        AccessDenied,       // アクセス拒否
        DeviceLost,         // デバイス喪失
        InitializeFailed,   // 初期化失敗
        CreateFailed,       // 作成失敗
        GetFailed,          // 取得失敗
        InvalidState,       // 無効な状態
        InternalError,      // 内部エラー
        UnknownError,       // 不明なエラー
    };

    /// @brief 結果コードを文字列へ変換
    /// @param a_code 変換対象の結果コード
    /// @return 結果コード名
    [[nodiscard]] inline const char* to_string(Code a_code) noexcept
    {
        switch (a_code)
        {
        case Code::OK: return "OK";
        case Code::InvalidArgument: return "InvalidArgument";
        case Code::NotFound: return "NotFound";
        case Code::Unsupported: return "Unsupported";
        case Code::OutOfMemory: return "OutOfMemory";
        case Code::AccessDenied: return "AccessDenied";
        case Code::DeviceLost: return "DeviceLost";
        case Code::InitializeFailed: return "InitializeFailed";
        case Code::CreateFailed: return "CreateFailed";
        case Code::GetFailed: return "GetFailed";
        case Code::InternalError: return "InternalError";
        case Code::UnknownError: return "UnknownError";
        default: return "UnknownCode";
        }
    }

    /// @brief 結果コードが成功かを判定
    /// @param a_code 判定対象の結果コード
    /// @return 成功なら true
    inline bool success(const Code& a_code) noexcept
    {
        return a_code == Code::OK;
    }

    /// @brief Level
    enum class Severity : uint8_t
    {
        Info = 0,   // 情報
        Warning,    // 警告
        Error,      // エラー
        Fatal,      // 致命的エラー
    };

    /// @brief 重大度を文字列へ変換
    /// @param a_severity 変換対象の重大度
    /// @return 重大度名
    [[nodiscard]] inline const char* to_string(Severity a_severity) noexcept
    {
        switch (a_severity)
        {
        case Severity::Info: return "Info";
        case Severity::Warning: return "Warning";
        case Severity::Error: return "Error";
        case Severity::Fatal: return "Fatal";
        default: return "UnknownSeverity";
        }
    }

    /// @brief 処理結果構造体
    struct Result final
    {
        explicit Result() = default;

        Code code{ Code::OK };
        Severity severity{ Severity::Info };

        // message は非所有なので、呼び出し側で文字列の寿命を保証する
        std::string_view message{};

        const char* file = "";
        const char* function = "";
        uint32_t line = 0;

        /// @brief 成功結果を構築
        /// @return 既定の成功結果
        static Result ok() noexcept
        {
            return Result{};
        }

        /// @brief Code::OK だけを成功として bool 化
        /// @return Code::OK の場合のみ true
        explicit operator bool() const noexcept
        {
            return code == Code::OK;
        }

        /// @brief 失敗結果を構築
        /// @param a_code 結果コード
        /// @param a_severity 重大度
        /// @param a_message メッセージ
        /// @param a_location 呼び出し位置
        /// @return 構築した失敗結果
        static Result fail(
            Code a_code,
            Severity a_severity,
            std::string_view a_message,
            const std::source_location& a_location = std::source_location::current()
        ) noexcept
        {
            Result result{};
            result.code = a_code;
            result.severity = a_severity;
            result.message = a_message;
            result.file = a_location.file_name();
            result.function = a_location.function_name();
            result.line = static_cast<uint32_t>(a_location.line());
            return result;
        }
    };
}
