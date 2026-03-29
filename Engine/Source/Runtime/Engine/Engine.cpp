#include "Engine.h"

namespace Cue
{
    Result Engine::initialize(const EngineSetupInfo& a_info)
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
        FrameControllerDesc desc{};
        desc.m_bufferCount = a_info.bufferCount;
        desc.m_mode = ControllerMode::Fixed;
        desc.m_maxFps = a_info.maxFps;
        m_frameController = std::make_unique<FrameController>(
            desc,
            m_platform->thread_factory(),
            m_platform->clock(),
            m_platform->waiter(),
            update(), render(), present());

        // FrameGraph の生成
        Result r = m_backend->create_frame_graph(m_frameGraph);
        if (!r)
        {
            return Result::fail(
                r.code, Severity::Fatal,
                "Failed to create frame graph from backend.");
        }
        r = m_backend->create_frame_graph(m_presentFrameGraph);
        if (!r)
        {
            return Result::fail(
                r.code, Severity::Fatal,
                "Failed to create present frame graph from backend.");
        }

        m_presentFrameGraph->add_pass(std::make_unique<RHI::ClearBackBufferPass>());
        m_presentFrameGraph->build();

        return Result::ok();
    }

    void Engine::shutdown()
    {
        // 1) 先にフレーム実行系を止めて、render/present ジョブが backend へ入らない状態にします。
        m_frameController.reset();

        // 2) FrameGraph を先に破棄して、view/pipeline の解放を backend shutdown より前に終わらせます。
        m_presentFrameGraph.reset();
        m_frameGraph.reset();

        // 3) 外部所有ポインタはここでは解放せず、参照だけ明示的に切ります。
        m_backend = nullptr;
        m_platform = nullptr;
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
                m_backend->render(a_frameNo, a_index, *m_frameGraph);
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index)
            {
                m_backend->present(a_frameNo, a_index, *m_presentFrameGraph);
            };
    }
}
