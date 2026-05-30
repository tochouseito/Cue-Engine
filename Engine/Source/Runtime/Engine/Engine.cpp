#include "Engine.h"

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === Engine includes ===
#include "Command/PlatformCommandContext.h"

namespace Cue
{
    Result Engine::initialize(EngineSetupInfo& a_info)
    {
        // 引数の検査
        if (a_info.platformCommandBridge == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Platform command bridge must not be null.");
        }

        // 依存オブジェクトの保存
        m_platformCommandBridge = a_info.platformCommandBridge;
        m_platform = a_info.platform;

        // フレームコントローラーの生成
        FrameControllerDesc desc(3);
        desc.mode = ControllerMode::Fixed;
        desc.maxFps = a_info.maxFps;
        m_frameController = std::make_unique<FrameController>(
            desc, m_platform->thread_factory(), m_platform->clock(),
            m_platform->waiter(), update(), render(), present(),
            [this]()
            {
                
            });

        return Result::ok();
    }

    void Engine::shutdown()
    {
        // フレームコントローラーの終了
        if (m_frameController != nullptr)
        {
            m_frameController->synchronize();
            m_frameController.reset();
        }

        // 依存オブジェクトの解放
        m_platformCommandBridge = nullptr;
    }

    Result Engine::begin_frame()
    {
        // フレーム開始処理

        // platform 由来の要求はフレーム先頭で回収し、OS 依存入力をここで閉じ込める
        if (m_platformCommandBridge)
        {
            PlatformCommandContext platformCommandContext(m_platformRuntimeState);
            Result result = m_platformCommandBridge->drain_commands(platformCommandContext);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result Engine::end_frame()
    {
        // フレーム終了処理
        return Result::ok();
    }

    Result Engine::tick()
    {
        // ティック処理

        m_frameController->step();

        return Result::ok();
    }
}
