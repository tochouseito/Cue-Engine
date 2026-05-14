#include "Engine.h"
#include "PlatformCommandContext.h"
#include "DrawSystem/Passes/DebugGridPass.h"
#include "DrawSystem/Passes/DebugDrawPass.h"
#include "DrawSystem/Passes/DebugObjectIdPass.h"
#include "DrawSystem/Passes/DebugOutlinePass.h"
#include "DrawSystem/Passes/DebugPickReadbackPass.h"
#include "DrawSystem/Passes/DebugSelectionPass.h"
#include "DrawSystem/Passes/GenerateVisibleList.h"
#include "DrawSystem/Passes/VisibleObjectBucketizePass.h"
#include "DrawSystem/Passes/MaterialBufferCopyPass.h"
#include "DrawSystem/Passes/RenderObjectCopyPass.h"
#include "DrawSystem/Passes/RenderableInfoCopyPass.h"
#include "DrawSystem/Passes/SpriteForwardPass.h"
#include "DrawSystem/Passes/SpriteInstanceCopyPass.h"
#include "DrawSystem/Passes/StaticMeshBatchingPass.h"
#include "DrawSystem/Passes/StaticMeshForwardPass.h"
#include "DrawSystem/Passes/TransformBufferCopyPass.h"
#include "DrawSystem/Passes/VisibleObjectCountCopyPass.h"
#include "DrawSystem/Passes/ViewProjectionCopyPass.h"
#include "LightingSystem/GpuData/LightData.h"
#include "LightingSystem/Passes/LightBufferCopyPass.h"
#include "ShadowSystem/GpuData/ShadowData.h"
#include "ShadowSystem/Passes/DirectionalShadowMapPass.h"
#include "ShadowSystem/Passes/PointShadowMapPass.h"
#include "ShadowSystem/Passes/ShadowBufferCopyPass.h"
#include "ShadowSystem/Passes/SpotShadowMapPass.h"
#include "ShadowSystem/Passes/SpotShadowMapPreviewPass.h"
#include "Script/ScriptRuntime.h"
#include <IO/Logger.h>
#include <PlatformCommands.h>
#include <PresentToSwapChain.h>
#include <IO/Path.h>

// === C++ includes ===
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace Cue
{
    Result Engine::initialize(EngineSetupInfo& a_info)
    {
        // 引数の検査
        if (!a_info.platform || !a_info.backend || !a_info.audioBackend)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Invalid argument: platform, backend, and audio backend must not be null");
        }

        // 依存オブジェクトの保存
        m_platform = a_info.platform;
        m_backend = a_info.backend;
        m_audioBackend = a_info.audioBackend;
        m_physicsSystem = a_info.physicsSystem;
        m_editorBridge = a_info.editorBridge;
        m_platformBridge = a_info.platformBridge;

        Audio::AudioDeviceDesc audioDeviceDesc{};
        Result result =
            m_audioBackend->create_device(audioDeviceDesc, m_audioDevice);
        if (!result)
        {
            return result;
        }

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

        auto* commandPool = m_backend->get_command_pool();
        auto* queuePool = m_backend->get_queue_pool();
        if (commandPool == nullptr || queuePool == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Fatal,
                "Failed to get command or queue pool from backend.");
        }

        auto* textureManager = m_backend->get_texture_manager();
        if (textureManager == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Fatal,
                "Failed to get texture manager from backend.");
        }

        DrawSystem::StaticMeshPoolDesc meshPoolDesc{};
        m_staticMeshPool = std::make_unique<DrawSystem::StaticMeshPool>(
            meshPoolDesc, *bufferManager, *viewManager, *commandPool, *queuePool);

        m_assetManager.initialize(m_staticMeshPool.get(), textureManager);

        Core::IO::Path errorTexturePath = a_info.errorTexturePath;
        if (errorTexturePath.is_empty())
        {
#ifdef CUE_PROJECT_ROOT_PATH
            errorTexturePath = Core::IO::Path::join(
                Core::IO::Path(std::string(CUE_PROJECT_ROOT_PATH)),
                Core::IO::Path("Engine/Textures/CueDummy.cuetexture"));
#else
            return Result::fail(Code::InvalidState, Severity::Fatal,
                "Error texture path is not configured for Engine.");
#endif
        }

        result = m_assetManager.register_error_texture_from_cuetexture(
            m_platform->file_system(),
            errorTexturePath);
        if (!result)
        {
            return result;
        }

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
            m_staticMeshPool->get_mesh_id(cubeStaticMeshHandle, m_defaultCubeMeshId);
        if (!result)
        {
            return result;
        }

        m_editorWorld = std::make_unique<GameCore::GameWorld>();
        result = m_editorWorld->initialize(
            bufferManager, viewManager, m_staticMeshPool.get(), &m_assetManager,
            &m_platform->file_system(), m_audioBackend, m_audioDevice,
            m_physicsSystem, &m_platform->input_manager(),
            m_backend->buffer_count(), m_backend->width(), m_backend->height(),
            m_defaultCubeMeshId, m_defaultMaterialHandle);
        if (!result)
        {
            return result;
        }

        m_activeWorld = m_editorWorld.get();

        m_scriptModuleHost =
            std::make_unique<ScriptModuleHost>(
                m_platform->file_system(), m_platform);
        result = m_scriptModuleHost->initialize(*m_activeWorld);
        if (!result)
        {
            return result;
        }

        result = create_render_target_resources(
            "GameColor",
            RHI::ColorFormat::R8G8B8A8_UNORM,
            m_gameRenderTarget);
        if (!result)
        {
            return result;
        }

        result = create_render_target_resources(
            "DebugColor",
            RHI::ColorFormat::R8G8B8A8_UNORM,
            m_debugRenderTarget);
        if (!result)
        {
            return result;
        }

        result = create_render_target_resources(
            "DebugObjectId",
            RHI::ColorFormat::R32_UINT,
            m_debugObjectIdTarget);
        if (!result)
        {
            return result;
        }

        result = create_render_target_resources(
            "DebugOutlineObjectId",
            RHI::ColorFormat::R32_UINT,
            m_debugOutlineObjectIdTarget);
        if (!result)
        {
            return result;
        }

        result = create_debug_pick_readback_buffer();
        if (!result)
        {
            return result;
        }

        result = create_view_projection_buffer(
            "DebugViewProjectionBuffer",
            m_debugViewProjectionBufferHandle,
            m_debugViewProjectionUploaders);
        if (!result)
        {
            return result;
        }
        const float debugAspectRatio =
            m_backend->height() > 0
            ? static_cast<float>(m_backend->width()) /
                static_cast<float>(m_backend->height())
            : 1.0f;
        const Math::float4x4 debugWorldMatrix = Math::make_affine_matrix(
            Math::float3(1.0f, 1.0f, 1.0f),
            Math::float3::zero(),
            Math::float3(0.0f, 2.0f, -6.0f));
        m_debugViewProjection.view = Math::float4x4::inverse(debugWorldMatrix);
        m_debugViewProjection.projection = Math::perspective_fov_matrix(
            60.0f * Math::k_pi / 180.0f,
            debugAspectRatio,
            0.1f,
            1000.0f);

        result = create_debug_selection_buffer();
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

        result = destroy_debug_view_projection_buffer();
        if (!result)
        {
            CUE_ASSERTF(false,
                "Failed to destroy debug view projection buffer: %s",
                result.message.data());
        }
        result = destroy_debug_selection_buffer();
        if (!result)
        {
            CUE_ASSERTF(false,
                "Failed to destroy debug selection buffer: %s",
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
        m_staticMeshPool.reset();

        if (m_audioBackend != nullptr && m_audioDevice.valid())
        {
            const Result destroyAudioResult =
                m_audioBackend->destroy_device(m_audioDevice);
            if (!destroyAudioResult)
            {
                CUE_ASSERTF(false, "Failed to destroy audio device: %s",
                    destroyAudioResult.message.data());
            }
        }
        m_audioDevice = {};
        m_audioBackend = nullptr;
        m_physicsSystem = nullptr;
    }

    Result Engine::begin_frame()
    {
        if (m_platform != nullptr)
        {
            Result platformResult = m_platform->begin_frame();
            if (!platformResult)
            {
                return platformResult;
            }
        }

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

    Result Engine::end_frame()
    {
        if (m_platform != nullptr)
        {
            return m_platform->end_frame();
        }

        return Result::ok();
    }

    Result Engine::tick()
    {
        m_frameController->step();

        return Result::ok();
    }

    Result Engine::create_render_target_resources(
        std::string_view a_name,
        RHI::ColorFormat a_format,
        RenderTargetResources& a_outResources)
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

        std::string colorName(a_name);
        RHI::TextureDesc colorDesc{};
        colorDesc.name = colorName;
        colorDesc.bufferCount = 1;
        colorDesc.kind = RHI::TextureKind::RenderTarget;
        colorDesc.width = m_backend->width();
        colorDesc.height = m_backend->height();
        colorDesc.format = a_format;
        Math::float4 clearColor = Math::float4::from_rgba8(63, 63, 63, 255);
        colorDesc.clearColor[0] = clearColor.r;
        colorDesc.clearColor[1] = clearColor.g;
        colorDesc.clearColor[2] = clearColor.b;
        colorDesc.clearColor[3] = clearColor.a;
        if (a_format == RHI::ColorFormat::R32_UINT)
        {
            colorDesc.clearColor[0] = 0.0f;
            colorDesc.clearColor[1] = 0.0f;
            colorDesc.clearColor[2] = 0.0f;
            colorDesc.clearColor[3] = 0.0f;
        }
        Result result =
            textureManager->create_texture(colorDesc, a_outResources.colorHandle);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc colorRtvDesc{};
        colorRtvDesc.name = colorName + "RTV";
        colorRtvDesc.type = RHI::ViewType::RenderTarget;
        colorRtvDesc.bufferKind = RHI::BufferKind::Texture;
        colorRtvDesc.textureHandle = a_outResources.colorHandle;
        colorRtvDesc.colorFormat = a_format;
        result = viewManager->create_view(
            colorRtvDesc, a_outResources.colorRtvHandle);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc colorSrvDesc{};
        colorSrvDesc.name = colorName + "SRV";
        colorSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
        colorSrvDesc.bufferKind = RHI::BufferKind::Texture;
        colorSrvDesc.textureHandle = a_outResources.colorHandle;
        colorSrvDesc.colorFormat = a_format;
        colorSrvDesc.mipLevels = 1;
        result = viewManager->create_view(
            colorSrvDesc, a_outResources.colorSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result Engine::destroy_render_target_resources(
        RenderTargetResources& a_resources)
    {
        auto* textureManager = m_backend ? m_backend->get_texture_manager() : nullptr;
        auto* viewManager = m_backend ? m_backend->get_view_manager() : nullptr;

        if (viewManager != nullptr)
        {
            if (a_resources.colorSrvHandle.valid())
            {
                Result result =
                    viewManager->destroy_view(a_resources.colorSrvHandle);
                if (!result)
                {
                    return result;
                }
                a_resources.colorSrvHandle = {};
            }

            if (a_resources.colorRtvHandle.valid())
            {
                Result result =
                    viewManager->destroy_view(a_resources.colorRtvHandle);
                if (!result)
                {
                    return result;
                }
                a_resources.colorRtvHandle = {};
            }
        }

        if (textureManager != nullptr && a_resources.colorHandle.valid())
        {
            Result result =
                textureManager->destroy_texture(a_resources.colorHandle);
            if (!result)
            {
                return result;
            }
            a_resources.colorHandle = {};
        }

        return Result::ok();
    }

    Result Engine::create_debug_pick_readback_buffer()
    {
        auto* bufferManager = m_backend ? m_backend->get_buffer_manager() : nullptr;
        if (bufferManager == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get buffer manager for debug pick readback buffer.");
        }

        constexpr uint32_t k_readbackStride = 256;
        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "DebugPickReadbackBuffer";
        bufferDesc.type = RHI::BufferType::Readback;
        bufferDesc.readbackHeapCount = m_backend->buffer_count();
        bufferDesc.initialState = RHI::ResourceState::CopyDest;
        bufferDesc.stride = k_readbackStride;
        bufferDesc.elementCount = 1;
        bufferDesc.size = k_readbackStride;
        bufferDesc.alignment = k_readbackStride;

        Result result = bufferManager->create_buffer(
            bufferDesc, m_debugPickReadbackBufferHandle);
        if (!result)
        {
            return result;
        }

        result = bufferManager->get_readback_buffer_view(
            m_debugPickReadbackBufferHandle,
            m_debugPickReadbackView);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result Engine::destroy_debug_pick_readback_buffer()
    {
        auto* bufferManager = m_backend ? m_backend->get_buffer_manager() : nullptr;
        m_debugPickReadbackView = {};
        m_debugPickState = {};
        m_hasDebugPickResult = false;
        m_debugPickResultEntityId = GameCore::k_invalidEntityId;
        if (bufferManager != nullptr && m_debugPickReadbackBufferHandle.valid())
        {
            Result result =
                bufferManager->destroy_buffer(m_debugPickReadbackBufferHandle);
            if (!result)
            {
                return result;
            }
            m_debugPickReadbackBufferHandle = {};
        }

        return Result::ok();
    }

    Result Engine::create_view_projection_buffer(
        std::string_view a_name,
        RHI::BufferHandle& a_outBufferHandle,
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>&
            a_outUploaders)
    {
        auto* bufferManager = m_backend ? m_backend->get_buffer_manager() : nullptr;
        if (bufferManager == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get buffer manager for view projection buffer.");
        }

        constexpr uint32_t k_constantBufferAlignment = 256;

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = std::string(a_name);
        bufferDesc.type = RHI::BufferType::Constant;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_backend->buffer_count();
        bufferDesc.initialState = RHI::ResourceState::Common;
        bufferDesc.stride = sizeof(GpuData::ViewProjectionGpu);
        bufferDesc.elementCount = 1;
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = k_constantBufferAlignment;

        Result result = bufferManager->create_buffer(bufferDesc, a_outBufferHandle);
        if (!result)
        {
            return result;
        }

        result = bufferManager->create_slot_uploaders(
            a_outBufferHandle, m_backend->buffer_count(), a_outUploaders);
        if (!result)
        {
            return result;
        }
        if (a_outUploaders.size() != m_backend->buffer_count())
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "View projection buffer uploader was not created.");
        }

        return Result::ok();
    }

    Result Engine::destroy_debug_view_projection_buffer()
    {
        auto* bufferManager = m_backend ? m_backend->get_buffer_manager() : nullptr;
        m_debugViewProjectionUploaders.clear();
        if (bufferManager != nullptr && m_debugViewProjectionBufferHandle.valid())
        {
            Result result =
                bufferManager->destroy_buffer(m_debugViewProjectionBufferHandle);
            if (!result)
            {
                return result;
            }
            m_debugViewProjectionBufferHandle = {};
        }

        return Result::ok();
    }

    Result Engine::create_debug_selection_buffer()
    {
        auto* bufferManager = m_backend ? m_backend->get_buffer_manager() : nullptr;
        if (bufferManager == nullptr)
        {
            return Result::fail(
                Code::NotFound,
                Severity::Fatal,
                "Failed to get buffer manager for debug selection buffer.");
        }

        constexpr uint32_t k_constantBufferAlignment = 256;

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "DebugSelectionBuffer";
        bufferDesc.type = RHI::BufferType::Constant;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_backend->buffer_count();
        bufferDesc.initialState = RHI::ResourceState::Common;
        bufferDesc.stride = sizeof(GpuData::DebugSelectionGpu);
        bufferDesc.elementCount = 1;
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = k_constantBufferAlignment;

        Result result = bufferManager->create_buffer(
            bufferDesc, m_debugSelectionBufferHandle);
        if (!result)
        {
            return result;
        }

        result = bufferManager->create_slot_uploaders(
            m_debugSelectionBufferHandle,
            m_backend->buffer_count(),
            m_debugSelectionUploaders);
        if (!result)
        {
            return result;
        }
        if (m_debugSelectionUploaders.size() != m_backend->buffer_count())
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "Debug selection buffer uploader was not created.");
        }

        m_debugSelection = GpuData::DebugSelectionGpu{};
        return Result::ok();
    }

    Result Engine::destroy_debug_selection_buffer()
    {
        auto* bufferManager = m_backend ? m_backend->get_buffer_manager() : nullptr;
        m_debugSelectionUploaders.clear();
        if (bufferManager != nullptr && m_debugSelectionBufferHandle.valid())
        {
            Result result =
                bufferManager->destroy_buffer(m_debugSelectionBufferHandle);
            if (!result)
            {
                return result;
            }
            m_debugSelectionBufferHandle = {};
        }

        return Result::ok();
    }

    Result Engine::upload_debug_view_projection(uint32_t a_bufferIndex)
    {
        if (!m_debugViewProjectionBufferHandle.valid())
        {
            return Result::ok();
        }
        if (a_bufferIndex >= m_debugViewProjectionUploaders.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Debug view projection upload buffer index is out of range.");
        }

        RHI::SlotUploader<GpuData::ViewProjectionGpu>& uploader =
            m_debugViewProjectionUploaders[a_bufferIndex];
        uploader.begin_frame();
        if (!uploader.push(0, m_debugViewProjection))
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to queue debug view projection upload.");
        }
        if (!uploader.commit())
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to commit debug view projection upload.");
        }

        return Result::ok();
    }

    Result Engine::upload_debug_selection(uint32_t a_bufferIndex)
    {
        if (!m_debugSelectionBufferHandle.valid())
        {
            return Result::ok();
        }
        if (a_bufferIndex >= m_debugSelectionUploaders.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Debug selection upload buffer index is out of range.");
        }

        RHI::SlotUploader<GpuData::DebugSelectionGpu>& uploader =
            m_debugSelectionUploaders[a_bufferIndex];
        uploader.begin_frame();
        if (!uploader.push(0, m_debugSelection))
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to queue debug selection upload.");
        }
        if (!uploader.commit())
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to commit debug selection upload.");
        }

        return Result::ok();
    }

    bool Engine::request_debug_pick(
        float a_normalizedX,
        float a_normalizedY) noexcept
    {
        if (m_backend == nullptr || !m_debugPickReadbackBufferHandle.valid())
        {
            return false;
        }
        if (m_debugPickState.isRequested || m_debugPickState.isInFlight)
        {
            return false;
        }

        const float x =
            (std::max)(0.0f, (std::min)(a_normalizedX, 1.0f));
        const float y =
            (std::max)(0.0f, (std::min)(a_normalizedY, 1.0f));
        const uint32_t width = (std::max)(m_backend->width(), 1u);
        const uint32_t height = (std::max)(m_backend->height(), 1u);
        m_debugPickState.x =
            (std::min)(static_cast<uint32_t>(x * width), width - 1u);
        m_debugPickState.y =
            (std::min)(static_cast<uint32_t>(y * height), height - 1u);
        m_debugPickState.isRequested = true;
        m_hasDebugPickResult = false;
        m_debugPickResultEntityId = GameCore::k_invalidEntityId;
        return true;
    }

    bool Engine::request_debug_pick_pixel(uint32_t a_x, uint32_t a_y) noexcept
    {
        if (m_backend == nullptr || !m_debugPickReadbackBufferHandle.valid())
        {
            return false;
        }
        if (m_debugPickState.isRequested || m_debugPickState.isInFlight)
        {
            return false;
        }

        const uint32_t width = (std::max)(m_backend->width(), 1u);
        const uint32_t height = (std::max)(m_backend->height(), 1u);
        m_debugPickState.x = (std::min)(a_x, width - 1u);
        m_debugPickState.y = (std::min)(a_y, height - 1u);
        m_debugPickState.isRequested = true;
        m_hasDebugPickResult = false;
        m_debugPickResultEntityId = GameCore::k_invalidEntityId;
        return true;
    }

    void Engine::cancel_debug_pick() noexcept
    {
        m_debugPickState = {};
        m_hasDebugPickResult = false;
        m_debugPickResultEntityId = GameCore::k_invalidEntityId;
    }

    bool Engine::consume_debug_pick_result(
        GameCore::EntityId& a_outEntityId) noexcept
    {
        a_outEntityId = GameCore::k_invalidEntityId;
        if (!m_hasDebugPickResult)
        {
            return false;
        }

        a_outEntityId = m_debugPickResultEntityId;
        m_hasDebugPickResult = false;
        return true;
    }

    void Engine::resolve_debug_pick_readback() noexcept
    {
        if (!m_debugPickState.isInFlight)
        {
            return;
        }
        if (m_debugPickState.framesUntilReadable > 0)
        {
            --m_debugPickState.framesUntilReadable;
            return;
        }
        if (m_debugPickState.readbackResourceIndex >=
            m_debugPickReadbackView.mappedDatas.size())
        {
            m_debugPickState = {};
            return;
        }

        const std::byte* mappedData =
            m_debugPickReadbackView.mappedDatas[m_debugPickState.readbackResourceIndex];
        if (mappedData == nullptr)
        {
            m_debugPickState = {};
            return;
        }

        uint32_t encodedObjectId = 0;
        std::memcpy(&encodedObjectId, mappedData, sizeof(encodedObjectId));
        GameCore::EntityId entityId = GameCore::k_invalidEntityId;
        if (encodedObjectId > 0 && m_activeWorld != nullptr)
        {
            const Result result = m_activeWorld->get_render_object_entity(
                encodedObjectId - 1u,
                entityId);
            if (!result)
            {
                entityId = GameCore::k_invalidEntityId;
            }
        }

        m_debugPickResultEntityId = entityId;
        m_hasDebugPickResult = true;
        m_debugPickState = {};
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
        presentFrameGraphDesc.waitForCompletion = true;
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
        const DrawSystem::DrawResources* drawResources = m_activeWorld->draw_resources();
        if (drawResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine world resources are not initialized.");
        }
        const LightingSystem::LightResources* lightResources =
            m_activeWorld->light_resources();
        if (lightResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine light resources are not initialized.");
        }
        const LightingSystem::LightingBindings lightingBindings =
            lightResources->bindings();
        const ShadowSystem::ShadowResources* shadowResources =
            m_activeWorld->shadow_resources();
        if (shadowResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine shadow resources are not initialized.");
        }
        const ShadowSystem::ShadowBindings shadowBindings =
            shadowResources->bindings();
        auto* bufferManager = m_backend->get_buffer_manager();
        if (bufferManager == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Engine buffer manager is not initialized.");
        }

        m_frameGraph.reset();

        RHI::FrameGraphDesc frameGraphDesc{};
        frameGraphDesc.usePresentQueue = false;
        frameGraphDesc.enableProfiling = true;
        frameGraphDesc.waitForCompletion = true;
        Result result = m_backend->create_frame_graph(frameGraphDesc, m_frameGraph);
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal,
                "Failed to create render frame graph.");
        }

        m_frameGraph->add_pass(std::make_unique<DrawSystem::RenderableInfoCopyPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->renderable_info_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::TransformBufferCopyPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->transform_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::ViewProjectionCopyPass>(
            drawResources->view_projection_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::ViewProjectionCopyPass>(
            m_debugViewProjectionBufferHandle,
            sizeof(GpuData::ViewProjectionGpu),
            "DebugViewProjectionCopy"));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::ViewProjectionCopyPass>(
            m_debugSelectionBufferHandle,
            sizeof(GpuData::DebugSelectionGpu),
            "DebugSelectionCopy"));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::MaterialBufferCopyPass>(
            drawResources->material_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::SpriteInstanceCopyPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->sprite_instance_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::RenderObjectCopyPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->render_object_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::VisibleObjectCountCopyPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->visible_object_count_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<LightingSystem::LightBufferCopyPass>(
            "LightFrameBufferCopy",
            lightingBindings.frameBuffer,
            sizeof(GpuData::LightFrameGpu)));
        m_frameGraph->add_pass(std::make_unique<LightingSystem::LightBufferCopyPass>(
            "DirectionalLightBufferCopy",
            lightingBindings.directionalLightBuffer,
            static_cast<uint64_t>(GpuData::k_maxDirectionalLightCount) *
                sizeof(GpuData::DirectionalLightGpu)));
        m_frameGraph->add_pass(std::make_unique<LightingSystem::LightBufferCopyPass>(
            "PointLightBufferCopy",
            lightingBindings.pointLightBuffer,
            static_cast<uint64_t>(GpuData::k_maxPointLightCount) *
                sizeof(GpuData::PointLightGpu)));
        m_frameGraph->add_pass(std::make_unique<LightingSystem::LightBufferCopyPass>(
            "SpotLightBufferCopy",
            lightingBindings.spotLightBuffer,
            static_cast<uint64_t>(GpuData::k_maxSpotLightCount) *
                sizeof(GpuData::SpotLightGpu)));
        m_frameGraph->add_pass(std::make_unique<ShadowSystem::ShadowBufferCopyPass>(
            "DirectionalShadowFrameBufferCopy",
            shadowBindings.directionalShadowFrameBuffer,
            sizeof(GpuData::DirectionalShadowFrameGpu)));
        m_frameGraph->add_pass(std::make_unique<ShadowSystem::ShadowBufferCopyPass>(
            "PointShadowFaceBufferCopy",
            shadowBindings.pointShadowFaceBuffer,
            static_cast<uint64_t>(GpuData::k_pointShadowFaceCount) *
                sizeof(GpuData::PointShadowFaceGpu)));
        m_frameGraph->add_pass(std::make_unique<ShadowSystem::ShadowBufferCopyPass>(
            "SpotShadowFrameBufferCopy",
            shadowBindings.spotShadowFrameBuffer,
            static_cast<uint64_t>(GpuData::k_maxSpotShadowCount) *
                sizeof(GpuData::SpotShadowFrameGpu)));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::GenerateVisibleListPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->renderable_info_buffer_handle(),
            drawResources->render_object_buffer_handle(),
            drawResources->visible_object_count_buffer_handle(),
            drawResources->renderable_info_buffer_srv_handle(),
            drawResources->render_object_buffer_uav_handle(),
            drawResources->visible_object_count_buffer_uav_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::VisibleObjectBucketCountPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->render_object_buffer_handle(),
            drawResources->visible_object_count_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::VisibleObjectBucketPrefixPass>(
            m_activeWorld->draw_frame_state()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::VisibleObjectBucketScatterPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->render_object_buffer_handle(),
            drawResources->visible_object_count_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::StaticMeshBatchingPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->render_object_buffer_handle(),
            drawResources->transform_buffer_handle(),
            drawResources->visible_object_count_buffer_handle()));
        m_frameGraph->add_pass(
            std::make_unique<ShadowSystem::DirectionalShadowMapPass>(
                m_activeWorld->draw_frame_state(),
                drawResources->render_object_buffer_handle(),
                drawResources->transform_buffer_handle(),
                drawResources->visible_object_count_buffer_handle(),
                shadowBindings,
                m_cubeIndexCount));
        m_frameGraph->add_pass(std::make_unique<ShadowSystem::PointShadowMapPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->render_object_buffer_handle(),
            drawResources->transform_buffer_handle(),
            drawResources->visible_object_count_buffer_handle(),
            shadowBindings,
            m_cubeIndexCount));
        m_frameGraph->add_pass(std::make_unique<ShadowSystem::SpotShadowMapPass>(
            m_activeWorld->draw_frame_state(),
            m_activeWorld->shadow_frame_state(),
            drawResources->render_object_buffer_handle(),
            drawResources->transform_buffer_handle(),
            drawResources->visible_object_count_buffer_handle(),
            shadowBindings,
            m_cubeIndexCount));
        m_frameGraph->add_pass(
            std::make_unique<ShadowSystem::SpotShadowMapPreviewPass>(
                shadowBindings));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::StaticMeshForwardPass>(
            "GameStaticMeshForward",
            "GameColor",
            "GameColorRTV",
            "GameSceneDepth",
            "GameSceneDepthDSV",
            m_activeWorld->draw_frame_state(),
            drawResources->render_object_buffer_handle(),
            drawResources->transform_buffer_handle(),
            drawResources->view_projection_buffer_handle(),
            drawResources->visible_object_count_buffer_handle(),
            drawResources->material_buffer_handle(),
            lightingBindings,
            shadowBindings,
            m_cubeIndexCount));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::SpriteForwardPass>(
            "GameSpriteForward",
            "GameColor",
            "GameColorRTV",
            m_activeWorld->draw_frame_state(),
            drawResources->sprite_instance_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::DebugDrawPass>(
            "GameDebugDraw",
            "GameColor",
            "GameColorRTV",
            "GameSceneDepth",
            "GameSceneDepthDSV",
            *m_activeWorld,
            *bufferManager,
            drawResources->view_projection_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::StaticMeshForwardPass>(
            "DebugStaticMeshForward",
            "DebugColor",
            "DebugColorRTV",
            "DebugSceneDepth",
            "DebugSceneDepthDSV",
            m_activeWorld->draw_frame_state(),
            drawResources->render_object_buffer_handle(),
            drawResources->transform_buffer_handle(),
            m_debugViewProjectionBufferHandle,
            drawResources->visible_object_count_buffer_handle(),
            drawResources->material_buffer_handle(),
            lightingBindings,
            shadowBindings,
            m_cubeIndexCount,
            &m_debugViewShadingMode));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::DebugObjectIdPass>(
            m_activeWorld->draw_frame_state(),
            drawResources->render_object_buffer_handle(),
            drawResources->transform_buffer_handle(),
            m_debugViewProjectionBufferHandle,
            drawResources->visible_object_count_buffer_handle(),
            m_cubeIndexCount,
            m_debugSelectedObjectId));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::DebugGridPass>(
            m_debugViewProjectionBufferHandle,
            m_isDebugGridVisible));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::DebugDrawPass>(
            "DebugViewDebugDraw",
            "DebugColor",
            "DebugColorRTV",
            "DebugSceneDepth",
            "DebugSceneDepthDSV",
            *m_activeWorld,
            *bufferManager,
            m_debugViewProjectionBufferHandle));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::DebugSelectionPass>(
            m_debugViewProjectionBufferHandle,
            m_debugSelectionBufferHandle));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::SpriteForwardPass>(
            "DebugSpriteForward",
            "DebugColor",
            "DebugColorRTV",
            m_activeWorld->draw_frame_state(),
            drawResources->sprite_instance_buffer_handle()));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::DebugOutlinePass>(
            m_debugSelectedObjectId));
        m_frameGraph->add_pass(std::make_unique<DrawSystem::DebugPickReadbackPass>(
            m_debugPickState,
            m_debugPickReadbackBufferHandle,
            (std::max)(m_backend->buffer_count(), 1u)));

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

    float Engine::simulation_delta_time(float a_rawDeltaTime) noexcept
    {
        constexpr float k_maxSimulationDeltaTime = 1.0f / 30.0f;

        if (!is_playing())
        {
            return 0.0f;
        }

        if (m_simulationWarmupFrames > 0)
        {
            --m_simulationWarmupFrames;
            return 0.0f;
        }

        if (a_rawDeltaTime <= 0.0f)
        {
            return 0.0f;
        }

        return (std::min)(a_rawDeltaTime, k_maxSimulationDeltaTime);
    }

    Result Engine::destroy_size_dependent_resources()
    {
        m_presentFrameGraph.reset();
        m_frameGraph.reset();
        Result result = destroy_render_target_resources(m_debugObjectIdTarget);
        if (!result)
        {
            return result;
        }

        result =
            destroy_render_target_resources(m_debugOutlineObjectIdTarget);
        if (!result)
        {
            return result;
        }

        result = destroy_debug_pick_readback_buffer();
        if (!result)
        {
            return result;
        }

        result = destroy_render_target_resources(m_debugRenderTarget);
        if (!result)
        {
            return result;
        }

        return destroy_render_target_resources(m_gameRenderTarget);
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

        result = destroy_render_target_resources(m_debugObjectIdTarget);
        if (!result)
        {
            return result;
        }

        result =
            destroy_render_target_resources(m_debugOutlineObjectIdTarget);
        if (!result)
        {
            return result;
        }

        result = destroy_render_target_resources(m_debugRenderTarget);
        if (!result)
        {
            return result;
        }

        result = destroy_render_target_resources(m_gameRenderTarget);
        if (!result)
        {
            return result;
        }

        result = m_backend->resize(request.width, request.height);
        if (!result)
        {
            return result;
        }

        result = create_render_target_resources(
            "GameColor",
            RHI::ColorFormat::R8G8B8A8_UNORM,
            m_gameRenderTarget);
        if (!result)
        {
            return result;
        }

        result = create_render_target_resources(
            "DebugColor",
            RHI::ColorFormat::R8G8B8A8_UNORM,
            m_debugRenderTarget);
        if (!result)
        {
            return result;
        }

        result = create_render_target_resources(
            "DebugObjectId",
            RHI::ColorFormat::R32_UINT,
            m_debugObjectIdTarget);
        if (!result)
        {
            return result;
        }

        result = create_render_target_resources(
            "DebugOutlineObjectId",
            RHI::ColorFormat::R32_UINT,
            m_debugOutlineObjectIdTarget);
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

            resolve_debug_pick_readback();

            const float rawDeltaTime =
                (m_frameController != nullptr)
                ? static_cast<float>(
                    m_frameController->frame_counter().delta_time())
                : 0.0f;
            const float deltaTime = simulation_delta_time(rawDeltaTime);

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

            Result debugCameraResult = upload_debug_view_projection(a_index);
            if (!debugCameraResult)
            {
                CUE_ASSERTF(false, "Debug camera upload failed: %s",
                    debugCameraResult.message.data());
                return;
            }
            Result debugSelectionResult = upload_debug_selection(a_index);
            if (!debugSelectionResult)
            {
                CUE_ASSERTF(false, "Debug selection upload failed: %s",
                    debugSelectionResult.message.data());
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
                bufferManager, viewManager, m_staticMeshPool.get(),
                &m_assetManager,
                &m_platform->file_system(), m_audioBackend, m_audioDevice,
                m_physicsSystem, &m_platform->input_manager(),
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
        m_playWorld->set_asset_root_path(m_assetRootPath);

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

        m_frameController->frame_counter().reset();
        m_simulationWarmupFrames = 2;
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
        m_simulationWarmupFrames = 0;

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
