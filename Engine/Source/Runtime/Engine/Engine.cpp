#include "Engine.h"

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === Engine includes ===
#include "Command/PlatformCommandContext.h"

// === Frame Passes includes ===
#include "DrawSystem/passes/PresentToSwapChain.h"

namespace Cue
{
    Result Engine::initialize(EngineSetupInfo& a_info)
    {
        Result r = Result::ok();

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
        m_renderBackend = a_info.renderBackend;

        // フレームコントローラーの生成
        FrameControllerDesc desc(a_info.renderBackend->buffer_count());
        desc.mode = ControllerMode::Backpressure;
        desc.maxFps = a_info.maxFps;
        m_frameController = std::make_unique<FrameController>(
            desc, m_platform->thread_factory(), m_platform->clock(),
            m_platform->waiter(), update(), render(), present(),
            [this]()
            {

            });

        //共有リソースの作成
        r = RHI::create_render_target_resources(
            *m_renderBackend,
            "FinalColor",
            RHI::ColorFormat::R8G8B8A8_UNORM,
            m_gameRenderTarget);
        if (!r)
        {
            return r;
        }

        // FrameGraph の生成
        r = create_frame_graphs(nullptr);
        if (!r)
        {
            return r;
        }

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

        if (m_renderBackend != nullptr)
        {
            Result waitResult = m_renderBackend->wait_for_idle();
            if (!waitResult)
            {
                CUE_ASSERT_FORMAT(false, "Failed to wait backend idle during shutdown: %s",
                    waitResult.message.data());
            }
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

    std::function<void(uint64_t, uint32_t)> Engine::render()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index) {
            a_frameNo; a_index; // 未使用パラメーターの警告回避   
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index) {
            m_renderBackend->present(a_frameNo, a_index, true, *m_presentFrameGraph);
            };
    }

    Result Engine::create_frame_graphs(std::unique_ptr<RHI::FrameGraphPass> a_editorPass)
    {
        Result result = Result::ok();

        RHI::FrameGraphDesc presentFrameGraphDesc{};
        presentFrameGraphDesc.usePresentQueue = true;
        presentFrameGraphDesc.enableProfiling = true;
        presentFrameGraphDesc.waitForCompletion = true;
        result =
            m_renderBackend->create_frame_graph(presentFrameGraphDesc, m_presentFrameGraph);
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal,
                "Failed to create present frame graph.");
        }

        if (a_editorPass)
        {
            m_presentFrameGraph->add_pass(std::move(a_editorPass));
        }
        else
        {
            m_presentFrameGraph->add_pass(
                std::make_unique<RHI::PresentToSwapChainPass>());
        }

        result = m_presentFrameGraph->build();
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }
}
