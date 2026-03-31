#include "Engine.h"
#include <PresentToSwapChain.h>

namespace Cue
{
    Result Engine::initialize(EngineSetupInfo& a_info)
    {
        // 引数の検査
        if (!a_info.platform || !a_info.backend)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid argument: platform and backend must not be null");
        }

        // 依存オブジェクトの保存
        m_platform = a_info.platform;
        m_backend = a_info.backend;

        // フレームコントローラーの生成
        FrameControllerDesc desc(m_backend->buffer_count());
        desc.m_mode = ControllerMode::Fixed;
        desc.m_maxFps = a_info.maxFps;
        m_frameController = std::make_unique<FrameController>(
            desc,
            m_platform->thread_factory(),
            m_platform->clock(),
            m_platform->waiter(),
            update(), render(), present());

        // present 用 FrameGraph の生成
        Result result = m_backend->create_frame_graph(m_presentFrameGraph);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Fatal,
                "Failed to create present frame graph.");
        }

        if (a_info.editorPass)
        {
            m_presentFrameGraph->add_pass(std::move(a_info.editorPass));
        }
        else
        {
            m_presentFrameGraph->add_pass(std::make_unique<RHI::PresentToSwapChainPass>());
        }

        result = m_presentFrameGraph->build();
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Fatal,
                "Failed to build present frame graph.");
        }

        return Result::ok();
    }

    void Engine::shutdown()
    {
        m_frameController.reset();
    }

    Result Engine::begin_frame()
    {
        return Result::ok();
    }

    Result Engine::end_frame()
    {
        return Result::ok();
    }

    Result Engine::tick()
    {
        m_frameController->step();

        return Result::ok();
    }

    std::function<void(uint64_t, uint32_t)> Engine::update()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index)
            {
                a_frameNo; a_index;
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::render()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index)
            {
                a_frameNo; a_index;
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index)
            {
                m_backend->present(a_frameNo, a_index, true, *m_presentFrameGraph);
            };
    }
}
