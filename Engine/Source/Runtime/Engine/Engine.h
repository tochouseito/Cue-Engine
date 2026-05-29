#pragma once

/// ********************************************************************************
/// エンジン
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === PAL includes ===
#include <PlatformRuntimeState.h>

namespace Cue
{
    struct EngineSetupInfo final
    {
        Core::CQRS::Bridge* platformCommandBridge = nullptr; // プラットフォームからコマンドを受け取るためのブリッジ
    };

    class Engine final
    {
    public:
        Engine() = default;
        // コピー禁止
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        // ムーブ禁止
        Engine(Engine&&) = delete;
        Engine& operator=(Engine&&) = delete;
        ~Engine() = default;

        /// @brief 初期化
        Result initialize(EngineSetupInfo& a_info);

        /// @brief 終了
        void shutdown();

        /// @brief フレーム開始処理
        Result begin_frame();

        /// @brief フレーム終了処理
        Result end_frame();

        /// @brief ティック処理
        Result tick();

    private:
        Core::CQRS::Bridge* m_platformCommandBridge = nullptr; // プラットフォームからコマンドを受け取るためのブリッジ
        PAL::PlatformRuntimeState m_platformRuntimeState; // プラットフォームランタイム状態
    };
}
