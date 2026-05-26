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
}
