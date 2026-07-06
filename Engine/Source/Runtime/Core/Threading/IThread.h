#pragma once

/// *********************************************************************************
/// スレッドインターフェース
/// *********************************************************************************

// === C++ includes ===
#include <cstddef>
#include <cstdint>
#include <string>

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include "StopToken.h"

namespace Cue::Core::Threading
{
    /// @brief Thread 起動時に要求する native apartment model
    enum class ThreadApartmentModel : uint32_t
    {
        None,
        SingleThreaded,
        MultiThreaded,
    };

    /// @brief スレッド生成時の指定内容
    struct ThreadDesc final
    {
        // デバッグ用途の名前（UTF-8想定）
        std::string name{};

        // スタックサイズ（0ならOS/CRTデフォルト）
        std::size_t stackSizeBytes = 0;

        // 優先度（実装側で解釈。0はデフォルト）
        int priority = 0;

        // アフィニティ（0なら未指定）
        uint64_t affinityMask = 0;

        // apartment model を持つ platform でのみ解釈する thread 単位の指定
        ThreadApartmentModel apartmentModel = ThreadApartmentModel::None;
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
        /// @brief スレッド終了を待機
        virtual Result join() noexcept = 0;

        /// @brief 停止要求を通知
        virtual void request_stop() noexcept = 0;
        /// @brief 停止監視用トークンを取得
        virtual StopToken stop_token() const noexcept = 0;

        /// @brief スレッド ID を返す
        virtual uint32_t thread_id() const noexcept = 0;
        /// @brief 終了コードを返す
        virtual uint32_t exit_code() const noexcept = 0;
    };
}
