#pragma once

#include <cstdint>
#include <string_view>
#include <source_location>

namespace Cue::Core
{
    enum class Facility : uint16_t
    {
        Core = 1,
        Platform = 2,
        IO = 3,
        Graphics = 4,
        GraphicsCore = 5,
        D3D12 = 6,
    };

    enum class Code : uint16_t
    {
        Ok = 0,
        InvalidArg, // 引数エラー
        InvalidState, // 状態エラー（例: 初期化前のオブジェクトに対する操作）
        NotFound, // 対象が見つからない
        AccessDenied, // アクセス拒否
        IoError, // 入出力エラー
        OutOfMemory, // メモリ不足
        Unsupported, // サポートされていない操作
        CreationFailed, // 作成失敗
        GettingInfoFailed, // 情報取得失敗
        Unknown, // 不明なエラー
    };

    enum class Severity : uint8_t
    {
        Info = 0,
        Warning = 1,
        Error = 2,
        Fatal = 3,
    };

    struct Result final
    {
        Facility facility = Facility::Core;
        Code code = Code::Ok;
        Severity severity = Severity::Info;

        // OS/SDKの生コード：GetLastError や HRESULT をそのまま入れる用途
        uint32_t native = 0;

        // 動的確保を避けるため、基本は静的文字列だけ（必要ならログ側で整形）
        std::string_view message{};

        // 任意：発生箇所（ログで使う）
        const char* file = "";
        const char* function = "";
        uint32_t line = 0;

        static Result ok() noexcept
        {
            // 1) 成功のデフォルト値を返す
            return {};
        }

        static Result fail(
            Facility f, Code c, Severity s, uint32_t nativeCode,
            std::string_view msg,
            const std::source_location& loc = std::source_location::current()) noexcept
        {
            // 1) 結果を組み立てる（動的確保はしない）
            Result r{};
            r.facility = f;
            r.code = c;
            r.severity = s;
            r.native = nativeCode;
            r.message = msg;

            // 2) 呼び出し位置を保存
            r.file = loc.file_name();
            r.function = loc.function_name();
            r.line = static_cast<uint32_t>(loc.line());

            return r;
        }

        explicit operator bool() const noexcept
        {
            // 1) 成功判定
            return code == Code::Ok;
        }
    };
} // namespace Cue::Core
