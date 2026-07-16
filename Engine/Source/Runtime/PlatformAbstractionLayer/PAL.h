#pragma once

/// ********************************************************************************
/// プラットフォーム抽象化レイヤー
/// ********************************************************************************

/// === C++ includes ===
#include <cstdint>

/// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <Threading/IThread.h>
#include <Threading/IThreadFactory.h>
#include <Time/IClock.h>
#include <Time/IWaiter.h>
#include <Time/Timer.h>
#include <CQRS/CQRS.h>

/// === PAL includes ===
#include "PALCommon.h"
#include "PlatformMessage.h"

namespace Cue::PAL
{
    /// @brief プラットフォーム共通初期化設定
    struct PlatformSetupInfo final
    {
        uint32_t width = 0; // ウィンドウ幅
        uint32_t height = 0; // ウィンドウ高さ
        const char* className = nullptr; // ウィンドウクラス名
        const char* title = nullptr; // ウィンドウタイトル
    };

    /// @brief プラットフォームインタフェース
    class IPlatform
    {
    public:
        virtual ~IPlatform() = default;

        /// @brief プラットフォーム実装を初期化
        /// @param a_info 初期化設定
        virtual Result initialize(const PlatformSetupInfo& a_info) = 0;
        /// @brief 実行状態へ遷移
        virtual Result start() = 0;
        /// @brief プラットフォーム実装を終了
        virtual Result shutdown() = 0;
        /// @brief プラットフォームの frame scope を開始
        virtual Result begin_frame() = 0;
        /// @brief プラットフォームの frame scope を終了
        virtual Result end_frame() = 0;
        /// @brief プラットフォームメッセージを 1 件取得
        virtual PlatformMessage poll_message() = 0;
    public:
        // --- 実行時サービス ---
        virtual Core::Threading::IThreadFactory& thread_factory() = 0;
        virtual Core::Time::IClock& clock() = 0;
        virtual Core::Time::IWaiter& waiter() = 0;
        virtual Core::IO::IFileSystem& file_system() = 0;
        virtual Result get_process_memory_usage(ProcessMemoryUsage& a_out) noexcept = 0;
        virtual Result get_system_memory_usage(SystemMemoryUsage& a_out) noexcept = 0;

        // プラットフォームコマンドはヘッドレステストで不要なため任意接続にする
        void set_command_bridge(Core::CQRS::Bridge* a_bridge) noexcept
        {
            m_commandBridge = a_bridge;
        }
    protected:
        Core::CQRS::Bridge* m_commandBridge = nullptr; // コマンドブリッジの非所有ポインタ
    };
}
