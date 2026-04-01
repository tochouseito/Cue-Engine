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

        // FinalColor を作成
        RHI::TextureDesc finalColorDesc{};
        finalColorDesc.name = "FinalColor";
        finalColorDesc.bufferCount = 1;
        finalColorDesc.kind = RHI::TextureKind::RenderTarget;
        finalColorDesc.width = m_backend->width();
        finalColorDesc.height = m_backend->height();
        finalColorDesc.format = RHI::ColorFormat::R8G8B8A8_UNORM;
        finalColorDesc.clearColor[0] = 0.0f;
        finalColorDesc.clearColor[1] = 0.5f;
        finalColorDesc.clearColor[2] = 0.0f;
        finalColorDesc.clearColor[3] = 1.0f;
        RHI::TextureHandle finalColorHandle{};
        auto textureManager = m_backend->get_texture_manager();
        textureManager->create_texture(finalColorDesc, finalColorHandle);
        RHI::ViewDesc finalColorRtvDesc{};
        finalColorRtvDesc.name = "FinalColorRTV";
        finalColorRtvDesc.type = RHI::ViewType::RenderTarget;
        finalColorRtvDesc.bufferKind = RHI::BufferKind::Texture;
        finalColorRtvDesc.textureHandle = finalColorHandle;
        finalColorRtvDesc.colorFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
        RHI::ViewHandle finalColorRtvHandle{};
        auto viewManager = m_backend->get_view_manager();
        viewManager->create_view(finalColorRtvDesc, finalColorRtvHandle);
        RHI::ViewDesc finalColorSrvDesc{};
        finalColorSrvDesc.name = "FinalColorSRV";
        finalColorSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
        finalColorSrvDesc.bufferKind = RHI::BufferKind::Texture;
        finalColorSrvDesc.textureHandle = finalColorHandle;
        finalColorSrvDesc.colorFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
        finalColorSrvDesc.mipLevels = 1;
        RHI::ViewHandle finalColorSrvHandle{};
        viewManager->create_view(finalColorSrvDesc, finalColorSrvHandle);

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
                m_backend->render(a_frameNo, a_index, *m_frameGraph);
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index)
            {
                m_backend->present(a_frameNo, a_index, false, *m_presentFrameGraph);
            };
    }
}
