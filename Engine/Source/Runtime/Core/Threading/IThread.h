// IThread の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstddef>
#include <cstdint>
#include <string>

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include "StopToken.h"

namespace Cue::Core::Threading
{
    /// @brief スレッド生成時の指定内容
    struct ThreadDesc final
    {
        // - デバッグ用途の名前（UTF-8想定）
        std::string name{};

        // - スタックサイズ（0ならOS/CRTデフォルト）
        std::size_t stackSizeBytes = 0;

        // - 優先度（実装側で解釈0はデフォルト）
        int priority = 0;

        // - アフィニティ（0なら未指定）
        uint64_t affinityMask = 0;
    };

    using threadProc = uint32_t(*)(StopToken a_token, void* a_user) noexcept;

    /// @brief スレッド実装を抽象化するインターフェース
    class IThread
    {
    public:
        IThread() noexcept = default;
        virtual ~IThread() = default;
        // コピー禁止
        IThread(const IThread&) = delete;
        IThread& operator=(const IThread&) = delete;
        // ムーブは実装側で定義しても良いが、ここでは禁止しておく
        IThread(IThread&&) = delete;
        IThread& operator=(IThread&&) = delete;

        /// @brief join 可能かを返す
        virtual bool joinable() const noexcept = 0;
        /// @brief スレッド終了を待機する
        virtual Result join() noexcept = 0;

        /// @brief 停止要求を通知する
        virtual void request_stop() noexcept = 0;
        /// @brief 停止監視用トークンを取得する
        virtual StopToken stop_token() const noexcept = 0;

        /// @brief スレッド ID を返す
        virtual uint32_t thread_id() const noexcept = 0;
        /// @brief 終了コードを返す
        virtual uint32_t exit_code() const noexcept = 0;
    };
}
