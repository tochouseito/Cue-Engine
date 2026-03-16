#pragma once

// C++ includes
#include <cstdint>
#include <string_view>
#include <source_location>

namespace Cue
{
    // 結果コード
    enum class Code : uint16_t
    {
        OK = 0,
        InvalidArgument, // 引数エラー
        NotFound,        // 見つからない
        Unsupported,      // 非対応
        InternalError,    // 内部エラー
        CreateFailed,     // 作成失敗
        GettingFailed,    // 取得失敗
        UnknownError,     // 不明なエラー
    };

    // 結果の重大度
    enum class Severity : uint8_t
    {
        Info = 0,
        Warning,
        Error,
        Fatal,
    };

    // 結果構造体
    struct Result final
    {
        // 暗黙変換禁止
        explicit Result() = default;

        Code code = Code::OK; // 結果コード
        Severity severity = Severity::Info; // 重大度

        // 生コード用
        uint32_t rawCode = 0;

        // メッセージ
        // -- 静的文字列前提
        // -- string_view で非所有
        std::string_view message{};

        // ソース位置
        const char* file = "";
        const char* function = "";
        uint32_t line = 0;

        // 成功のデフォルト値を返す
        static Result ok() noexcept
        {
            return {};
        }

        // エラーを作成
        static Result fail(
            Code c, Severity s, std::string_view msg,
            uint32_t rawC = 0, const std::source_location& loc = std::source_location::current()
        ) noexcept
        {
            // 1) 結果を組み立てる
            Result r{};
            r.code = c;
            r.severity = s;
            r.message = msg;
            r.rawCode = rawC;
            r.file = loc.file_name();
            r.function = loc.function_name();
            r.line = static_cast<uint32_t>(loc.line());
            return r;
        }

        // bool 変換
        // -- OK なら true、それ以外は false
        // -- 暗黙変換禁止
        explicit operator bool() const noexcept
        {
            return code == Code::OK;
        }
    };

} // namespace Cue
