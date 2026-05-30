#pragma once

/// ********************************************************************************
/// エンジン
/// ********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === PAL includes ===
#include <PAL.h>
#include <PlatformRuntimeState.h>

// === Engine includes ===
#include "FrameController.h"

// === C++ includes ===
#include <cstdint>
#include <memory>

namespace Cue
{
    struct EngineSetupInfo final
    {
        PAL::IPlatform* platform = nullptr; // プラットフォームインターフェース
        Core::CQRS::Bridge* platformCommandBridge = nullptr; // プラットフォームからコマンドを受け取るためのブリッジ
        uint32_t maxFps = 60; // 最大フレームレート
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

        /// @brief 更新
        std::function<void(uint64_t, uint32_t)> update()
        {
            return [this](uint64_t frameNo, uint32_t updateIndex)
                {
                    frameNo; updateIndex; // 未使用パラメーターの警告回避
                };
        }
        /// @brief 描画
        std::function<void(uint64_t, uint32_t)> render()
        {
            return [this](uint64_t frameNo, uint32_t renderIndex)
                {
                    frameNo; renderIndex; // 未使用パラメーターの警告回避
                };
        }
        /// @brief present
        std::function<void(uint64_t, uint32_t)> present()
        {
            return [this](uint64_t frameNo, uint32_t presentIndex)
                {
                    frameNo; presentIndex; // 未使用パラメーターの警告回避
                };
        }
    private:
        std::unique_ptr<FrameController> m_frameController = nullptr; // フレームコントローラー
        PAL::IPlatform* m_platform = nullptr; // プラットフォームインターフェースの非所有ポインタ
        Core::CQRS::Bridge* m_platformCommandBridge = nullptr; // プラットフォームからコマンドを受け取るためのブリッジ
        PAL::PlatformRuntimeState m_platformRuntimeState; // プラットフォームランタイム状態
    };
}
