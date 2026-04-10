#include "Engine.h"
#include "Passes/GenerateVisibleList.h"
#include "Passes/ObjectInfoCopyPass.h"
#include "Passes/StaticMeshBatchingPass.h"
#include "Passes/StaticMeshForwardPass.h"
#include "Passes/TransformBufferCopyPass.h"
#include <PlatformCommands.h>
#include <PresentToSwapChain.h>

namespace Cue
{
    class PlatformCommandContext final : public PAL::IPlatformCommandContext
    {
    public:
        explicit PlatformCommandContext(PAL::PlatformRuntimeState& a_state) noexcept
            : m_state(a_state)
        {
        }

        Result request_window_resize(uint32_t a_width, uint32_t a_height) override
        {
            return m_state.request_window_resize(a_width, a_height);
        }

    private:
        PAL::PlatformRuntimeState& m_state;
    };

    Result Engine::initialize(EngineSetupInfo& a_info)
    {
        // 引数の検査
        if (!a_info.platform || !a_info.backend)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid argument: platform and backend must not be null");
        }

        // 依存オブジェクトの保存
        m_platform = a_info.platform;
        m_backend = a_info.backend;
        m_editorBridge = a_info.editorBridge;
        m_platformBridge = a_info.platformBridge;

        auto* staticMeshPool = m_backend->get_static_mesh_pool();
        if (staticMeshPool == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Fatal,
                "Failed to get static mesh pool from backend.");
        }

        m_assetManager.initialize(staticMeshPool);

        Result result{};
        ModelHandle cubeModelHandle{};
        result = m_assetManager.create_cube_model(cubeModelHandle);
        if (!result)
        {
            return result;
        }

        Core::Native::ModelData cubeModelData{};
        result = m_assetManager.get_model(cubeModelHandle, cubeModelData);
        if (!result)
        {
            return result;
        }
        if (cubeModelData.meshes.empty())
        {
            return Result::fail(Code::NotFound, Severity::Fatal,
                "Cube model does not contain any mesh data.");
        }
        const uint32_t cubeIndexCount =
            static_cast<uint32_t>(cubeModelData.meshes[0].indices.size());

        // GenerateVisibleObjectList 用バッファを作成
        auto* bufferManager = m_backend->get_buffer_manager();
        if (bufferManager == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Fatal,
                "Failed to get buffer manager from backend.");
        }

        auto* viewManager = m_backend->get_view_manager();
        if (viewManager == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Fatal,
                "Failed to get view manager from backend.");
        }

        m_gameCore = std::make_unique<GameCore>();
        result = m_gameCore->initialize(
            bufferManager, viewManager, m_backend->buffer_count());
        if (!result)
        {
            return result;
        }

        result = m_gameCore->add_object();
        if (!result)
        {
            return result;
        }

        result = m_gameCore->add_object();
        if (!result)
        {
            return result;
        }

        result = m_gameCore->add_object();
        if (!result)
        {
            return result;
        }

        // FinalColor を作成
        RHI::TextureDesc finalColorDesc{};
        finalColorDesc.name = "FinalColor";
        finalColorDesc.bufferCount = 1;
        finalColorDesc.kind = RHI::TextureKind::RenderTarget;
        finalColorDesc.width = m_backend->width();
        finalColorDesc.height = m_backend->height();
        finalColorDesc.format = RHI::ColorFormat::R8G8B8A8_UNORM;
        Math::float4 clearColor = Math::float4::from_rgba8(63, 63, 63, 255);
        finalColorDesc.clearColor[0] = clearColor.r;
        finalColorDesc.clearColor[1] = clearColor.g;
        finalColorDesc.clearColor[2] = clearColor.b;
        finalColorDesc.clearColor[3] = clearColor.a;
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

        // render 用 FrameGraph の生成
        RHI::FrameGraphDesc frameGraphDesc{};
        frameGraphDesc.usePresentQueue = false;
        result = m_backend->create_frame_graph(frameGraphDesc, m_frameGraph);
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal,
                "Failed to create render frame graph.");
        }

        m_frameGraph->add_pass(std::make_unique<ObjectInfoCopyPass>(
            m_gameCore->render_scene_state()));
        m_frameGraph->add_pass(std::make_unique<TransformBufferCopyPass>(
            m_gameCore->render_scene_state()));
        m_frameGraph->add_pass(std::make_unique<GenerateVisibleListPass>(
            m_gameCore->render_scene_state()));
        m_frameGraph->add_pass(std::make_unique<StaticMeshBatchingPass>(
            m_gameCore->render_scene_state()));
        m_frameGraph->add_pass(std::make_unique<StaticMeshForwardPass>(
            m_gameCore->render_scene_state(), cubeIndexCount));
        result = m_frameGraph->build();
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal,
                "Failed to build render frame graph.");
        }

        // present 用 FrameGraph の生成
        RHI::FrameGraphDesc presentFrameGraphDesc{};
        presentFrameGraphDesc.usePresentQueue = true;
        result =
            m_backend->create_frame_graph(presentFrameGraphDesc, m_presentFrameGraph);
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal,
                "Failed to create present frame graph.");
        }

        if (a_info.editorPass)
        {
            m_presentFrameGraph->add_pass(std::move(a_info.editorPass));
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

        result = m_gameCore->update(0.0f, 0);
        if (!result)
        {
            return result;
        }

        // フレームコントローラーの生成
        FrameControllerDesc desc(m_backend->buffer_count());
        desc.mode = ControllerMode::Fixed;
        desc.maxFps = a_info.maxFps;
        m_frameController = std::make_unique<FrameController>(
            desc, m_platform->thread_factory(), m_platform->clock(),
            m_platform->waiter(), update(), render(), present());

        return Result::ok();
    }

    void Engine::shutdown() { m_frameController.reset(); }

    Result Engine::begin_frame()
    {
        // platform 由来の要求はフレーム先頭で回収し、OS 依存入力をここで閉じ込める。
        if (m_platformBridge)
        {
            PlatformCommandContext platformCommandContext(m_platformRuntimeState);
            Result result = m_platformBridge->drain_commands(platformCommandContext);
            if (!result)
            {
                return result;
            }
        }

        // editor ブリッジがあれば command を処理
        if (m_editorBridge)
        {
            EngineCommandContext commandContext(*m_gameCore);
            Result result = m_editorBridge->drain_commands(commandContext);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result Engine::end_frame() { return Result::ok(); }

    Result Engine::tick()
    {
        m_frameController->step();

        return Result::ok();
    }

    std::function<void(uint64_t, uint32_t)> Engine::update()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index) {
            (void)a_frameNo;

            const float deltaTime =
                (m_frameController != nullptr)
                ? static_cast<float>(
                    m_frameController->frame_counter().delta_time())
                : 0.0f;

            Result updateResult = m_gameCore->update(deltaTime, a_index);
            if (!updateResult)
            {
                CUE_ASSERTF(false, "GameCore update failed: %s",
                    updateResult.message.data());
                return;
            }
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::render()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index) {
            Result r = m_backend->render(a_frameNo, a_index, *m_frameGraph);
            if (!r)
            {
                CUE_ASSERTF(false, "Render failed: %s (code: %u, severity: %u)",
                    r.message.data(), static_cast<uint32_t>(r.code),
                    static_cast<uint32_t>(r.severity));
            }
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index) {
            m_backend->present(a_frameNo, a_index, false, *m_presentFrameGraph);
            };
    }

} // namespace Cue
