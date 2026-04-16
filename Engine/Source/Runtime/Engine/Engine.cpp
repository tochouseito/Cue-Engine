#include "Engine.h"
#include "Passes/GenerateVisibleList.h"
#include "Passes/RenderableInfoCopyPass.h"
#include "Passes/StaticMeshBatchingPass.h"
#include "Passes/StaticMeshForwardPass.h"
#include "Passes/TransformBufferCopyPass.h"
#include "Passes/ViewProjectionCopyPass.h"
#include <IO/Logger.h>
#include <PlatformCommands.h>
#include <PresentToSwapChain.h>

// === C++ includes ===
#include <array>
#include <cstddef>
#include <vector>

namespace Cue
{
    class PlatformCommandContext final : public PAL::IPlatformCommandContext
    {
    public:
        PlatformCommandContext(PAL::PlatformRuntimeState& a_state,
            FrameController* a_frameController) noexcept
            : m_state(a_state)
            , m_frameController(a_frameController)
        {
        }

        Result request_window_resize(uint32_t a_width, uint32_t a_height) override
        {
            Result result = m_state.request_window_resize(a_width, a_height);
            if (!result)
            {
                return result;
            }

            if (m_frameController != nullptr)
            {
                m_frameController->poll_resize_request();
            }

            return Result::ok();
        }

    private:
        PAL::PlatformRuntimeState& m_state;
        FrameController* m_frameController = nullptr;
    };

    namespace
    {
        [[nodiscard]] Result copy_file(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationPath) noexcept
        {
            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(a_sourcePath, &fileData);
            if (!result)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "Script module ファイルの読み込みに失敗しました。");
            }

            result = a_fileSystem.write_all(
                a_destinationPath,
                std::span<const std::byte>(fileData.data(), fileData.size()),
                true);
            if (!result)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    "Script module shadow copy の書き込みに失敗しました。");
            }

            return Result::ok();
        }

        [[nodiscard]] Result create_script_module_shadow_copy(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_scriptRoot,
            const Core::IO::Path& a_modulePath,
            uint64_t a_shadowCopyId,
            Core::IO::Path& a_outShadowModulePath) noexcept
        {
            const Core::IO::Path shadowDirectory = Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path("Intermediate/ScriptRuntime"));
            const std::string shadowBaseName =
                a_modulePath.stem() + "_" + std::to_string(a_shadowCopyId);

            a_outShadowModulePath = Core::IO::Path::join(
                shadowDirectory,
                Core::IO::Path(shadowBaseName + a_modulePath.extension()));

            Result result = copy_file(
                a_fileSystem, a_modulePath, a_outShadowModulePath);
            if (!result)
            {
                return result;
            }

            const Core::IO::Path sourcePdbPath = Core::IO::Path::join(
                a_modulePath.parent(),
                Core::IO::Path(a_modulePath.stem() + ".pdb"));
            bool sourcePdbExists = false;
            result = a_fileSystem.exists(sourcePdbPath, &sourcePdbExists);
            if (!result)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "Script module PDB の確認に失敗しました。");
            }

            if (!sourcePdbExists)
            {
                return Result::ok();
            }

            const Core::IO::Path shadowPdbPath = Core::IO::Path::join(
                shadowDirectory,
                Core::IO::Path(shadowBaseName + ".pdb"));
            return copy_file(a_fileSystem, sourcePdbPath, shadowPdbPath);
        }
    }

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

        m_gameWorld = std::make_unique<GameCore::GameWorld>();
        result = m_gameWorld->initialize(
            bufferManager, viewManager, m_backend->buffer_count(),
            m_backend->width(), m_backend->height(), m_defaultCubeMeshId);
        if (!result)
        {
            return result;
        }

        m_scriptModule = std::make_unique<ScriptModule>();
        m_scriptRuntime = std::make_unique<ScriptRuntime>(*m_gameWorld);

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

        result = m_gameWorld->update(
            0.0f, 0, m_backend->width(), m_backend->height());
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
        m_scriptRuntime.reset();
        m_scriptModule.reset();
        m_frameController.reset();
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
            EngineCommandContext commandContext(*m_gameWorld, m_editorSceneId);
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
        RHI::FrameGraphDesc frameGraphDesc{};
        frameGraphDesc.usePresentQueue = false;
        Result result = m_backend->create_frame_graph(frameGraphDesc, m_frameGraph);
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal,
                "Failed to create render frame graph.");
        }

        m_frameGraph->add_pass(std::make_unique<RenderableInfoCopyPass>(
            m_gameWorld->render_scene_state()));
        m_frameGraph->add_pass(std::make_unique<TransformBufferCopyPass>(
            m_gameWorld->render_scene_state()));
        m_frameGraph->add_pass(std::make_unique<ViewProjectionCopyPass>());
        m_frameGraph->add_pass(std::make_unique<GenerateVisibleListPass>(
            m_gameWorld->render_scene_state()));
        m_frameGraph->add_pass(std::make_unique<StaticMeshBatchingPass>(
            m_gameWorld->render_scene_state()));
        m_frameGraph->add_pass(std::make_unique<StaticMeshForwardPass>(
            m_gameWorld->render_scene_state(), m_cubeIndexCount));
        result = m_frameGraph->build();
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal,
                "Failed to build render frame graph.");
        }

        RHI::FrameGraphDesc presentFrameGraphDesc{};
        presentFrameGraphDesc.usePresentQueue = true;
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

            if (m_scriptRuntime != nullptr)
            {
                Result scriptResult = m_scriptRuntime->update(deltaTime);
                if (!scriptResult)
                {
                    CUE_ASSERTF(false, "Script runtime update failed: %s",
                        scriptResult.message.data());
                    return;
                }
            }

            Result updateResult = m_gameWorld->update(deltaTime, a_index,
                m_backend->width(), m_backend->height());
            if (!updateResult)
            {
                CUE_ASSERTF(false, "GameWorld update failed: %s",
                    updateResult.message.data());
                return;
            }
            };
    }

    Result Engine::load_script_module(const Core::IO::Path& a_scriptRoot) noexcept
    {
        if (m_platform == nullptr || m_scriptModule == nullptr || m_scriptRuntime == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine script runtime is not initialized.");
        }

        Core::IO::Path modulePath{};
        Result result = resolve_script_module_path(a_scriptRoot, modulePath);
        if (!result)
        {
            return result;
        }

        Core::IO::Path shadowModulePath{};
        result = create_script_module_shadow_copy(
            m_platform->file_system(),
            a_scriptRoot,
            modulePath,
            ++m_scriptModuleShadowCopyId,
            shadowModulePath);
        if (!result)
        {
            return result;
        }

        std::unique_ptr<ScriptModule> nextModule = std::make_unique<ScriptModule>();
        result = nextModule->load_shadow_copy(modulePath, shadowModulePath);
        if (!result)
        {
            return result;
        }

        {
            ScriptRuntime validationRuntime(*m_gameWorld);
            result = nextModule->register_scripts(validationRuntime.engine_api());
        }
        m_scriptRuntime->activate();
        if (!result)
        {
            nextModule->unload();
            return result;
        }

        result = m_scriptRuntime->reset();
        if (!result)
        {
            nextModule->unload();
            return result;
        }

        std::unique_ptr<ScriptModule> previousModule = std::move(m_scriptModule);
        m_scriptModule = std::move(nextModule);
        m_scriptRuntime->set_module(m_scriptModule.get());
        result = m_scriptModule->register_scripts(m_scriptRuntime->engine_api());
        if (!result)
        {
            m_scriptRuntime->set_module(nullptr);
            m_scriptModule->unload();
            m_scriptModule = std::move(previousModule);
            if (m_scriptModule != nullptr)
            {
                m_scriptRuntime->set_module(m_scriptModule.get());
                const Result restoreResult =
                    m_scriptModule->register_scripts(m_scriptRuntime->engine_api());
                if (!restoreResult)
                {
                    m_scriptRuntime->set_module(nullptr);
                    m_scriptModule->unload();
                    m_scriptModule = nullptr;
                    m_scriptRoot = {};
                }
            }
            return result;
        }

        if (previousModule != nullptr)
        {
            previousModule->unload();
        }

        m_scriptRoot = a_scriptRoot;
        return Result::ok();
    }

    void Engine::unload_script_module() noexcept
    {
        if (m_scriptRuntime != nullptr)
        {
            (void)m_scriptRuntime->reset();
            m_scriptRuntime->set_module(nullptr);
        }

        if (m_scriptModule != nullptr)
        {
            m_scriptModule->unload();
        }

        m_scriptRoot = {};
    }

    Result Engine::resolve_script_module_path(
        const Core::IO::Path& a_scriptRoot,
        Core::IO::Path& a_outModulePath) noexcept
    {
        a_outModulePath = {};
        if (m_platform == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Platform is not initialized.");
        }

#if defined(CUE_DEBUG)
        constexpr const char* k_buildConfig = "Debug";
#elif defined(CUE_RELWITHDEBINFO)
        constexpr const char* k_buildConfig = "RelWithDebInfo";
#else
        constexpr const char* k_buildConfig = "Release";
#endif

        const std::array<Core::IO::Path, 4> candidatePaths = {
            Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path(std::string("Binaries/") + k_buildConfig + "/GameScript.dll")),
            Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path(std::string("out/build/win-x64/GameScript/") +
                    k_buildConfig + "/GameScript.dll")),
            Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path(std::string("out/build/win-x64/") +
                    k_buildConfig + "/GameScript.dll")),
            Core::IO::Path::join(
                a_scriptRoot,
                Core::IO::Path(std::string("generated/outputs/") +
                    k_buildConfig + "/GameScript.dll")),
        };

        Core::IO::IFileSystem& fileSystem = m_platform->file_system();
        for (const Core::IO::Path& candidatePath : candidatePaths)
        {
            bool exists = false;
            Result result = fileSystem.exists(candidatePath, &exists);
            if (!result)
            {
                return result;
            }

            if (exists)
            {
                a_outModulePath = candidatePath;
                return Result::ok();
            }
        }

        return Result::fail(Code::NotFound, Severity::Warning,
            "GameScript.dll was not found in the script root.");
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
