#include "Engine.h"
#include "PlatformCommandContext.h"
#include "Passes/GenerateVisibleList.h"
#include "Passes/MaterialBufferCopyPass.h"
#include "Passes/RenderObjectCopyPass.h"
#include "Passes/RenderableInfoCopyPass.h"
#include "Passes/StaticMeshBatchingPass.h"
#include "Passes/StaticMeshForwardPass.h"
#include "Passes/TransformBufferCopyPass.h"
#include "Passes/VisibleObjectCountCopyPass.h"
#include "Passes/ViewProjectionCopyPass.h"
#include "Script/ScriptRuntime.h"
#include <IO/Logger.h>
#include <PlatformCommands.h>
#include <PresentToSwapChain.h>

// === C++ includes ===
#include <array>
#include <cstddef>
#include <vector>

namespace Cue
{
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
        m_defaultMaterialHandle = MaterialHandle{};
        result = m_assetManager.create_color_material(
            "DefaultWhite",
            Math::float4(1.0f, 1.0f, 1.0f, 1.0f),
            m_defaultMaterialHandle);
        if (!result)
        {
            return result;
        }

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
        m_cubeIndexCount =
            static_cast<uint32_t>(cubeModelData.meshes[0].indices.size());

        RHI::StaticMeshHandle cubeStaticMeshHandle{};
        result = m_assetManager.get_static_mesh_handle(
            cubeModelHandle, 0, cubeStaticMeshHandle);
        if (!result)
        {
            return result;
        }

        result =
            staticMeshPool->get_mesh_id(cubeStaticMeshHandle, m_defaultCubeMeshId);
        if (!result)
        {
            return result;
        }

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

        m_editorWorld = std::make_unique<GameCore::GameWorld>();
        result = m_editorWorld->initialize(
            bufferManager, viewManager, staticMeshPool, &m_assetManager,
            m_backend->buffer_count(), m_backend->width(), m_backend->height(),
            m_defaultCubeMeshId, m_defaultMaterialHandle);
        if (!result)
        {
            return result;
        }

        m_activeWorld = m_editorWorld.get();

        m_scriptModuleHost =
            std::make_unique<ScriptModuleHost>(m_platform->file_system());
        result = m_scriptModuleHost->initialize(*m_activeWorld);
        if (!result)
        {
            return result;
        }

        result = create_final_color_resources();
        if (!result)
        {
            return result;
        }

        result = create_frame_graphs(std::move(a_info.editorPass));
        if (!result)
        {
            return result;
        }

        result = m_activeWorld->editor_update(
            0, m_backend->width(), m_backend->height());
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
            m_platform->waiter(), update(), render(), present(),
            [this]()
            {
                Result resizeResult = apply_pending_resize();
                if (!resizeResult)
                {
                    CUE_ASSERTF(false, "Resize failed: %s",
                        resizeResult.message.data());
                }
            });

        return Result::ok();
    }

    void Engine::shutdown()
    {
        unload_script_module();
        m_scriptModuleHost.reset();

        if (m_frameController != nullptr)
        {
            m_frameController->synchronize();
            m_frameController.reset();
        }

        if (m_backend != nullptr)
        {
            Result waitResult = m_backend->wait_for_idle();
            if (!waitResult)
            {
                CUE_ASSERTF(false, "Failed to wait backend idle during shutdown: %s",
                    waitResult.message.data());
            }
        }

        Result result = destroy_size_dependent_resources();
        if (!result)
        {
            CUE_ASSERTF(false, "Failed to destroy size dependent resources: %s",
                result.message.data());
        }

        if (m_playWorld != nullptr)
        {
            const Result finalizeResult = m_playWorld->finalize_systems();
            if (!finalizeResult)
            {
                CUE_ASSERTF(false, "Failed to finalize play world systems: %s",
                    finalizeResult.message.data());
            }
        }
        if (m_editorWorld != nullptr)
        {
            const Result finalizeResult = m_editorWorld->finalize_systems();
            if (!finalizeResult)
            {
                CUE_ASSERTF(false, "Failed to finalize editor world systems: %s",
                    finalizeResult.message.data());
            }
        }

        m_activeWorld = nullptr;
        m_playWorld.reset();
        m_editorWorld.reset();
    }

    Result Engine::begin_frame()
    {
        // platform 由来の要求はフレーム先頭で回収し、OS 依存入力をここで閉じ込める。
        if (m_platformBridge)
        {
            PlatformCommandContext platformCommandContext(
                m_platformRuntimeState, m_frameController.get());
            Result result = m_platformBridge->drain_commands(platformCommandContext);
            if (!result)
            {
                return result;
            }
        }

        // editor ブリッジがあれば command を処理
        if (m_editorBridge)
        {
            EngineCommandContext commandContext(*m_editorWorld, m_editorSceneId);
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

    Result Engine::create_final_color_resources()
    {
        auto* textureManager = m_backend->get_texture_manager();
        auto* viewManager = m_backend->get_view_manager();
        if (textureManager == nullptr || viewManager == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get texture or view manager for size dependent resources.");
        }

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
        Result result =
            textureManager->create_texture(finalColorDesc, m_finalColorHandle);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc finalColorRtvDesc{};
        finalColorRtvDesc.name = "FinalColorRTV";
        finalColorRtvDesc.type = RHI::ViewType::RenderTarget;
        finalColorRtvDesc.bufferKind = RHI::BufferKind::Texture;
        finalColorRtvDesc.textureHandle = m_finalColorHandle;
        finalColorRtvDesc.colorFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
        result = viewManager->create_view(finalColorRtvDesc, m_finalColorRtvHandle);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc finalColorSrvDesc{};
        finalColorSrvDesc.name = "FinalColorSRV";
        finalColorSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
        finalColorSrvDesc.bufferKind = RHI::BufferKind::Texture;
        finalColorSrvDesc.textureHandle = m_finalColorHandle;
        finalColorSrvDesc.colorFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
        finalColorSrvDesc.mipLevels = 1;
        result = viewManager->create_view(finalColorSrvDesc, m_finalColorSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }
    Result Engine::destroy_final_color_resources()
    {
        auto* textureManager = m_backend ? m_backend->get_texture_manager() : nullptr;
        auto* viewManager = m_backend ? m_backend->get_view_manager() : nullptr;

        if (viewManager != nullptr)
        {
            if (m_finalColorSrvHandle.valid())
            {
                Result result = viewManager->destroy_view(m_finalColorSrvHandle);
                if (!result)
                {
                    return result;
                }
                m_finalColorSrvHandle = {};
            }

            if (m_finalColorRtvHandle.valid())
            {
                Result result = viewManager->destroy_view(m_finalColorRtvHandle);
                if (!result)
                {
                    return result;
                }
                m_finalColorRtvHandle = {};
            }
        }

        if (textureManager != nullptr && m_finalColorHandle.valid())
        {
            Result result = textureManager->destroy_texture(m_finalColorHandle);
            if (!result)
            {
                return result;
            }
            m_finalColorHandle = {};
        }

        return Result::ok();
    }
    Result Engine::create_frame_graphs(std::unique_ptr<RHI::FrameGraphPass> a_editorPass)
    {
        Result result = recreate_render_frame_graph();
        if (!result)
        {
            return result;
        }

        RHI::FrameGraphDesc presentFrameGraphDesc{};
        presentFrameGraphDesc.usePresentQueue = true;
        presentFrameGraphDesc.enableProfiling = true;
        result =
            m_backend->create_frame_graph(presentFrameGraphDesc, m_presentFrameGraph);
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
    Result Engine::recreate_render_frame_graph()
    {
        if (m_backend == nullptr || m_activeWorld == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine active world is not initialized.");
        }
        const WorldResources* worldResources = m_activeWorld->world_resources();
        if (worldResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine world resources are not initialized.");
        }

        m_frameGraph.reset();

        RHI::FrameGraphDesc frameGraphDesc{};
        frameGraphDesc.usePresentQueue = false;
        frameGraphDesc.enableProfiling = true;
        Result result = m_backend->create_frame_graph(frameGraphDesc, m_frameGraph);
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal,
                "Failed to create render frame graph.");
        }

        m_frameGraph->add_pass(std::make_unique<RenderableInfoCopyPass>(
            m_activeWorld->render_scene_state(),
            worldResources->renderable_info_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<TransformBufferCopyPass>(
            m_activeWorld->render_scene_state(),
            worldResources->transform_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<ViewProjectionCopyPass>(
            worldResources->view_projection_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<MaterialBufferCopyPass>(
            worldResources->material_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<RenderObjectCopyPass>(
            m_activeWorld->render_scene_state(),
            worldResources->render_object_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<VisibleObjectCountCopyPass>(
            m_activeWorld->render_scene_state(),
            worldResources->visible_object_count_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<GenerateVisibleListPass>(
            m_activeWorld->render_scene_state(),
            worldResources->renderable_info_buffer_handle(),
            worldResources->render_object_buffer_handle(),
            worldResources->visible_object_count_buffer_handle(),
            worldResources->renderable_info_buffer_srv_handle(),
            worldResources->render_object_buffer_uav_handle(),
            worldResources->visible_object_count_buffer_uav_handle()));
        m_frameGraph->add_pass(std::make_unique<StaticMeshBatchingPass>(
            m_activeWorld->render_scene_state(),
            worldResources->render_object_buffer_handle(),
            worldResources->transform_buffer_handle(),
            worldResources->visible_object_count_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<StaticMeshForwardPass>(
            m_activeWorld->render_scene_state(),
            worldResources->render_object_buffer_handle(),
            worldResources->transform_buffer_handle(),
            worldResources->view_projection_buffer_handle(),
            worldResources->visible_object_count_buffer_handle(),
            worldResources->material_buffer_handle(),
            m_cubeIndexCount));

        result = m_frameGraph->build();
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }
    Result Engine::sync_active_world_buffers()
    {
        if (m_backend == nullptr || m_activeWorld == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine active world is not initialized.");
        }

        for (uint32_t bufferIndex = 0; bufferIndex < m_backend->buffer_count();
             ++bufferIndex)
        {
            Result result = m_activeWorld->editor_update(
                bufferIndex, m_backend->width(), m_backend->height());
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }
    Result Engine::destroy_size_dependent_resources()
    {
        m_presentFrameGraph.reset();
        m_frameGraph.reset();
        return destroy_final_color_resources();
    }
    Result Engine::apply_pending_resize()
    {
        PAL::PendingResizeRequest request{};
        if (!m_platformRuntimeState.consume_pending_resize_request(request))
        {
            return Result::ok();
        }

        if (request.width == m_backend->width() && request.height == m_backend->height())
        {
            return Result::ok();
        }

        Result result = m_backend->wait_for_idle();
        if (!result)
        {
            return result;
        }

        result = destroy_final_color_resources();
        if (!result)
        {
            return result;
        }

        result = m_backend->resize(request.width, request.height);
        if (!result)
        {
            return result;
        }

        result = create_final_color_resources();
        if (!result)
        {
            return result;
        }

        result = m_frameGraph->rebuild(m_backend->width(), m_backend->height());
        if (!result)
        {
            return result;
        }

        return m_presentFrameGraph->rebuild(m_backend->width(), m_backend->height());
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

            if (is_playing() &&
                m_scriptModuleHost != nullptr &&
                m_scriptModuleHost->runtime() != nullptr)
            {
                Result scriptResult =
                    m_scriptModuleHost->runtime()->update(deltaTime);
                if (!scriptResult)
                {
                    CUE_ASSERTF(false, "Script runtime update failed: %s",
                        scriptResult.message.data());
                    return;
                }
            }

            if (is_playing())
            {
                Result simulateResult = m_activeWorld->simulate(deltaTime);
                if (!simulateResult)
                {
                    CUE_ASSERTF(false, "GameWorld simulate failed: %s",
                        simulateResult.message.data());
                    return;
                }
            }

            Result updateResult = m_activeWorld->editor_update(a_index,
                m_backend->width(), m_backend->height());
            if (!updateResult)
            {
                CUE_ASSERTF(false, "GameWorld editor update failed: %s",
                    updateResult.message.data());
                return;
            }
            };
    }

    Result Engine::load_script_module(
        const Core::IO::Path& a_scriptRoot,
        ScriptModuleBuildConfiguration a_configuration) noexcept
    {
        if (m_scriptModuleHost == nullptr || m_activeWorld == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine script runtime is not initialized.");
        }

        Result result =
            m_scriptModuleHost->load_module(
                a_scriptRoot, a_configuration, *m_activeWorld);
        if (!result)
        {
            return result;
        }

        m_scriptRoot = a_scriptRoot;
        return Result::ok();
    }

    Result Engine::load_static_script_module(
        CueScriptAbiVersion(CUE_SCRIPT_CALL* a_getAbiVersion)(void),
        CueResult(CUE_SCRIPT_CALL* a_getExports)(CueScriptExports*)) noexcept
    {
        if (m_scriptModuleHost == nullptr || m_activeWorld == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine script runtime is not initialized.");
        }

        Result result = m_scriptModuleHost->load_static_module(
            a_getAbiVersion, a_getExports, *m_activeWorld);
        if (!result)
        {
            return result;
        }

        m_scriptRoot = Core::IO::Path("[static]");
        return Result::ok();
    }

    void Engine::unload_script_module() noexcept
    {
        if (m_scriptModuleHost != nullptr)
        {
            m_scriptModuleHost->unload_module();
        }

        m_scriptRoot = {};
    }

    Result Engine::start_play_mode() noexcept
    {
        if (m_editorWorld == nullptr || m_activeWorld == nullptr || m_backend == nullptr ||
            m_frameController == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine play mode dependencies are not initialized.");
        }

        if (is_playing())
        {
            return Result::ok();
        }

        m_frameController->synchronize();

        Result result = m_backend->wait_for_idle();
        if (!result)
        {
            return result;
        }

        if (m_playWorld == nullptr)
        {
            m_playWorld = std::make_unique<GameCore::GameWorld>();
            auto* bufferManager = m_backend->get_buffer_manager();
            auto* viewManager = m_backend->get_view_manager();
            if (bufferManager == nullptr || viewManager == nullptr)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "Failed to get managers for play world.");
            }

            result = m_playWorld->initialize(
                bufferManager, viewManager, m_backend->get_static_mesh_pool(),
                &m_assetManager,
                m_backend->buffer_count(),
                m_backend->width(), m_backend->height(), m_defaultCubeMeshId,
                m_defaultMaterialHandle);
            if (!result)
            {
                m_playWorld.reset();
                return result;
            }
        }

        result = m_playWorld->clone_from(*m_editorWorld);
        if (!result)
        {
            return result;
        }

        m_activeWorld = m_playWorld.get();

        if (m_scriptModuleHost != nullptr)
        {
            result = m_scriptModuleHost->set_game_world(*m_activeWorld);
            if (!result)
            {
                m_activeWorld = m_editorWorld.get();
                return result;
            }
            m_scriptModuleHost->activate_runtime();
        }

        result = sync_active_world_buffers();
        if (!result)
        {
            m_activeWorld = m_editorWorld.get();
            return result;
        }

        result = recreate_render_frame_graph();
        if (!result)
        {
            m_activeWorld = m_editorWorld.get();
            return result;
        }

        return Result::ok();
    }

    Result Engine::stop_play_mode() noexcept
    {
        if (m_editorWorld == nullptr || m_backend == nullptr || m_frameController == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine play mode dependencies are not initialized.");
        }

        if (!is_playing())
        {
            return Result::ok();
        }

        m_frameController->synchronize();

        Result result = m_backend->wait_for_idle();
        if (!result)
        {
            return result;
        }

        m_activeWorld = m_editorWorld.get();

        if (m_scriptModuleHost != nullptr)
        {
            result = m_scriptModuleHost->set_game_world(*m_activeWorld);
            if (!result)
            {
                return result;
            }
            m_scriptModuleHost->activate_runtime();
        }

        result = sync_active_world_buffers();
        if (!result)
        {
            return result;
        }

        result = recreate_render_frame_graph();
        if (!result)
        {
            return result;
        }

        m_playWorld.reset();
        return Result::ok();
    }

    bool Engine::is_playing() const noexcept
    {
        return m_playWorld != nullptr && m_activeWorld == m_playWorld.get();
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
