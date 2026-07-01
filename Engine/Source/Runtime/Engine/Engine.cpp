#include "Engine.h"

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>
#include <Native/EngineNativeStruct.h>

// === Engine includes ===
#include "Command/PlatformCommandContext.h"
#include "DrawSystem/StaticMeshBatcher.h"
#include "DrawSystem/Systems/CameraSystem.h"
#include "DrawSystem/Systems/RenderableObjectSystem.h"

// === Frame Passes includes ===
#include "DrawSystem/passes/DrawResourceUploadCopyPass.h"
#include "DrawSystem/passes/PresentToSwapChain.h"
#include "DrawSystem/passes/StaticMeshIndirectPass.h"

// === C++ includes ===
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>

namespace
{
Cue::Core::Native::MeshData make_test_cube_mesh()
{
    using Cue::Math::float2;
    using Cue::Math::float3;
    using Cue::Math::float4;

    Cue::Core::Native::MeshData mesh{};
    mesh.name = "TestCube";

    // 面ごとに法線と UV を持てるよう、cube は 24 頂点で登録する。
    mesh.positions = {
        float4(-1.0f, -1.0f, -1.0f, 1.0f), float4(-1.0f, 1.0f, -1.0f, 1.0f), float4(1.0f, 1.0f, -1.0f, 1.0f),
        float4(1.0f, -1.0f, -1.0f, 1.0f),  float4(1.0f, -1.0f, 1.0f, 1.0f),  float4(1.0f, 1.0f, 1.0f, 1.0f),
        float4(-1.0f, 1.0f, 1.0f, 1.0f),   float4(-1.0f, -1.0f, 1.0f, 1.0f), float4(-1.0f, -1.0f, 1.0f, 1.0f),
        float4(-1.0f, 1.0f, 1.0f, 1.0f),   float4(-1.0f, 1.0f, -1.0f, 1.0f), float4(-1.0f, -1.0f, -1.0f, 1.0f),
        float4(1.0f, -1.0f, -1.0f, 1.0f),  float4(1.0f, 1.0f, -1.0f, 1.0f),  float4(1.0f, 1.0f, 1.0f, 1.0f),
        float4(1.0f, -1.0f, 1.0f, 1.0f),   float4(-1.0f, 1.0f, -1.0f, 1.0f), float4(-1.0f, 1.0f, 1.0f, 1.0f),
        float4(1.0f, 1.0f, 1.0f, 1.0f),    float4(1.0f, 1.0f, -1.0f, 1.0f),  float4(-1.0f, -1.0f, 1.0f, 1.0f),
        float4(-1.0f, -1.0f, -1.0f, 1.0f), float4(1.0f, -1.0f, -1.0f, 1.0f), float4(1.0f, -1.0f, 1.0f, 1.0f),
    };
    mesh.normals = {
        float3(0.0f, 0.0f, -1.0f), float3(0.0f, 0.0f, -1.0f), float3(0.0f, 0.0f, -1.0f), float3(0.0f, 0.0f, -1.0f),
        float3(0.0f, 0.0f, 1.0f),  float3(0.0f, 0.0f, 1.0f),  float3(0.0f, 0.0f, 1.0f),  float3(0.0f, 0.0f, 1.0f),
        float3(-1.0f, 0.0f, 0.0f), float3(-1.0f, 0.0f, 0.0f), float3(-1.0f, 0.0f, 0.0f), float3(-1.0f, 0.0f, 0.0f),
        float3(1.0f, 0.0f, 0.0f),  float3(1.0f, 0.0f, 0.0f),  float3(1.0f, 0.0f, 0.0f),  float3(1.0f, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),  float3(0.0f, 1.0f, 0.0f),  float3(0.0f, 1.0f, 0.0f),  float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, -1.0f, 0.0f), float3(0.0f, -1.0f, 0.0f), float3(0.0f, -1.0f, 0.0f), float3(0.0f, -1.0f, 0.0f),
    };
    mesh.uvs = {
        float2(0.0f, 1.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f),
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f), float2(0.0f, 0.0f),
        float2(1.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f),
        float2(1.0f, 1.0f), float2(0.0f, 1.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
        float2(0.0f, 1.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
    };
    mesh.indices = {
        0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
        12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };

    return mesh;
}
} // namespace

namespace Cue
{
Result Engine::initialize(EngineSetupInfo& a_info)
{
    Result r = Result::ok();

    // 引数の検査
    if (a_info.platformCommandBridge == nullptr)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Platform command bridge must not be null.");
    }
    if (a_info.platform == nullptr)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Platform must not be null.");
    }
    if (a_info.renderBackend == nullptr)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Render backend must not be null.");
    }

    // 依存オブジェクトの保存
    m_platformCommandBridge = a_info.platformCommandBridge;
    m_platform = a_info.platform;
    m_renderBackend = a_info.renderBackend;
    m_bufferCount = a_info.renderBackend->buffer_count();
    m_debugRenderView = a_info.debugRenderView;
    m_isDebugRenderingEnabled = a_info.editorPass != nullptr && m_debugRenderView != nullptr;

    // FrameState の初期化
    m_drawFrameState.resize(m_bufferCount);
    m_drawScenes.resize(m_bufferCount);
    if (m_isDebugRenderingEnabled)
    {
        m_debugDrawFrameState.resize(m_bufferCount);
        m_debugDrawScenes.resize(m_bufferCount);
    }
    for (uint32_t frameIndex = 0; frameIndex < m_bufferCount; ++frameIndex)
    {
        DrawSystem::DrawFrameData& frameState = m_drawFrameState.frame_state(frameIndex);
        frameState.renderWidth = m_renderBackend->width();
        frameState.renderHeight = m_renderBackend->height();
        frameState.objectCount = 0;
    }

    // フレームコントローラーの生成
    FrameControllerDesc desc(m_bufferCount);
    desc.mode = ControllerMode::Backpressure;
    desc.maxFps = a_info.maxFps;
    m_frameController = std::make_unique<FrameController>(
        desc, m_platform->thread_factory(), m_platform->clock(), m_platform->waiter(), update(), render(), present(),
        [this]()
        {
            Result r = apply_pending_resize();
            CUE_ASSERT_FORMAT(success(r), "Failed to apply pending resize: {}", r.message.data());
        });

    // 共有リソースの作成
    r = RHI::create_render_target_resources(*m_renderBackend, "FinalColor", RHI::ColorFormat::R8G8B8A8_UNORM,
                                            m_finalColorRenderTarget, Math::float4::from_rgba8(63, 63, 63, 255).data());
    if (!r)
    {
        return r;
    }

    if (m_isDebugRenderingEnabled)
    {
        r = RHI::create_render_target_resources(*m_renderBackend, "DebugColor", RHI::ColorFormat::R8G8B8A8_UNORM,
                                                m_debugColorRenderTarget,
                                                Math::float4::from_rgba8(63, 63, 63, 255).data());
        if (!r)
        {
            return r;
        }
    }

    auto* bufferManager = m_renderBackend->get_buffer_manager();
    if (bufferManager == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Fatal, "Failed to get buffer manager from backend.");
    }

    auto* viewManager = m_renderBackend->get_view_manager();
    if (viewManager == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Fatal, "Failed to get view manager from backend.");
    }

    auto* commandPool = m_renderBackend->get_command_pool();
    auto* queuePool = m_renderBackend->get_queue_pool();
    if (commandPool == nullptr || queuePool == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Fatal, "Failed to get command or queue pool from backend.");
    }

    // MeshPool の生成
    DrawSystem::MeshPoolDesc meshPoolDesc{};
    meshPoolDesc.maxVertexCount = 8u * 1024u * 1024u;
    meshPoolDesc.maxIndexCount = 16u * 1024u * 1024u;
    m_meshPool =
        std::make_unique<DrawSystem::MeshPool>(meshPoolDesc, *bufferManager, *viewManager, *commandPool, *queuePool);

    // 描画用リソース作成
    m_drawResources = std::make_unique<DrawSystem::DrawResources>(bufferManager, viewManager, m_bufferCount);
    m_maxObjectCount = k_maxObjectCount;
    m_maxCellCount = (m_maxObjectCount + k_cellObjectCapacity - 1u) / k_cellObjectCapacity;

    r = m_drawResources->create_renderable_info_buffer(m_maxObjectCount);
    if (!r)
    {
        return r;
    }

    r = m_drawResources->create_transform_buffer(m_maxObjectCount);
    if (!r)
    {
        return r;
    }

    r = m_drawResources->create_view_projection_buffer();
    if (!r)
    {
        return r;
    }

    r = m_drawResources->create_material_buffer(m_maxObjectCount);
    if (!r)
    {
        return r;
    }

    r = m_drawResources->create_render_cell_buffer(m_maxCellCount);
    if (!r)
    {
        return r;
    }

    r = m_drawResources->create_render_object_buffer(m_maxObjectCount);
    if (!r)
    {
        return r;
    }

    r = m_drawResources->create_object_count_buffer();
    if (!r)
    {
        return r;
    }

    r = m_drawResources->create_static_mesh_batch_buffers(m_maxObjectCount, m_maxObjectCount);
    if (!r)
    {
        return r;
    }

    if (m_isDebugRenderingEnabled)
    {
        m_debugDrawResources = std::make_unique<DrawSystem::DrawResources>(bufferManager, viewManager, m_bufferCount);

        r = m_debugDrawResources->create_renderable_info_buffer(m_maxObjectCount);
        if (!r)
        {
            return r;
        }

        r = m_debugDrawResources->create_transform_buffer(m_maxObjectCount);
        if (!r)
        {
            return r;
        }

        r = m_debugDrawResources->create_view_projection_buffer();
        if (!r)
        {
            return r;
        }

        r = m_debugDrawResources->create_material_buffer(m_maxObjectCount);
        if (!r)
        {
            return r;
        }

        r = m_debugDrawResources->create_render_cell_buffer(m_maxCellCount);
        if (!r)
        {
            return r;
        }

        r = m_debugDrawResources->create_render_object_buffer(m_maxObjectCount);
        if (!r)
        {
            return r;
        }

        r = m_debugDrawResources->create_object_count_buffer();
        if (!r)
        {
            return r;
        }

        r = m_debugDrawResources->create_static_mesh_batch_buffers(m_maxObjectCount, m_maxObjectCount);
        if (!r)
        {
            return r;
        }
    }

    r = initialize_render_extraction_pipeline();
    if (!r)
    {
        return r;
    }

    r = initialize_test_scene();
    if (!r)
    {
        return r;
    }

    // FrameGraph の構築
    r = create_frame_graphs(std::move(a_info.editorPass));
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
            CUE_ASSERT_FORMAT(false, "Failed to wait backend idle during shutdown: %s", waitResult.message.data());
        }

        Result destroyResult = RHI::destroy_render_target_resources(*m_renderBackend, m_finalColorRenderTarget);
        if (!destroyResult)
        {
            CUE_ASSERT_FORMAT(false, "Failed to destroy final color render target: %s", destroyResult.message.data());
        }

        destroyResult = RHI::destroy_render_target_resources(*m_renderBackend, m_debugColorRenderTarget);
        if (!destroyResult)
        {
            CUE_ASSERT_FORMAT(false, "Failed to destroy debug color render target: %s", destroyResult.message.data());
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
        PlatformCommandContext platformCommandContext(m_platformRuntimeState, m_frameController.get());
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

std::function<void(uint64_t, uint32_t)> Engine::update()
{
    return [this](uint64_t a_frameNo, uint32_t a_index)
    {
        (void)a_frameNo;

        Result result = update_draw_scene(a_index);
        CUE_ASSERT_FORMAT(success(result), "Failed to update draw scene: {}", result.message.data());
    };
}

std::function<void(uint64_t, uint32_t)> Engine::render()
{
    return [this](uint64_t a_frameNo, uint32_t a_index)
    {
        if (m_renderBackend != nullptr && m_frameGraph != nullptr)
        {
            (void)m_renderBackend->render(a_frameNo, a_index, *m_frameGraph);
        }
        if (m_renderBackend != nullptr && m_debugFrameGraph != nullptr)
        {
            (void)m_renderBackend->render(a_frameNo, a_index, *m_debugFrameGraph);
        }
    };
}

std::function<void(uint64_t, uint32_t)> Engine::present()
{
    return [this](uint64_t a_frameNo, uint32_t a_index)
    {
        if (m_renderBackend != nullptr && m_presentFrameGraph != nullptr)
        {
            (void)m_renderBackend->present(a_frameNo, a_index, false, *m_presentFrameGraph);
        }
    };
}

Result Engine::create_frame_graphs(std::unique_ptr<RHI::FrameGraphPass> a_editorPass)
{
    Result result = Result::ok();

    // メインのフレームグラフの構築
    RHI::FrameGraphDesc renderFrameGraphDesc{};
    renderFrameGraphDesc.usePresentQueue = true;
    renderFrameGraphDesc.enableProfiling = true;
    renderFrameGraphDesc.waitForCompletion = true;
    result = m_renderBackend->create_frame_graph(renderFrameGraphDesc, m_frameGraph);
    if (!result)
    {
        return Result::fail(result.code, Severity::Fatal, "Failed to create render frame graph.");
    }

    // メインのフレームグラフにパスを追加
    m_frameGraph->add_pass(std::make_unique<DrawSystem::DrawResourceUploadCopyPass>(*m_drawResources));
    m_frameGraph->add_pass(
        std::make_unique<DrawSystem::StaticMeshIndirectPass>(*m_drawResources, *m_meshPool, m_drawFrameState));

    // editor パスが存在する場合は、メインのフレームグラフに追加
    if (a_editorPass)
    {

    }

    // グラフを構築
    result = m_frameGraph->build();
    if (!result)
    {
        return result;
    }

    if (m_isDebugRenderingEnabled)
    {
        if (m_debugDrawResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Fatal, "Debug draw resources are not initialized.");
        }

        // DebugView 用のフレームグラフ。Main と同じ描画 pass を DebugColor へ向ける。
        RHI::FrameGraphDesc debugFrameGraphDesc{};
        debugFrameGraphDesc.usePresentQueue = true;
        debugFrameGraphDesc.enableProfiling = true;
        debugFrameGraphDesc.waitForCompletion = true;
        result = m_renderBackend->create_frame_graph(debugFrameGraphDesc, m_debugFrameGraph);
        if (!result)
        {
            return Result::fail(result.code, Severity::Fatal, "Failed to create debug frame graph.");
        }

        m_debugFrameGraph->add_pass(std::make_unique<DrawSystem::DrawResourceUploadCopyPass>(*m_debugDrawResources));
        m_debugFrameGraph->add_pass(
            std::make_unique<DrawSystem::StaticMeshIndirectPass>(
                *m_debugDrawResources,
                *m_meshPool,
                m_debugDrawFrameState,
                "DebugStaticMeshIndirect",
                "DebugColor"));

        result = m_debugFrameGraph->build();
        if (!result)
        {
            return result;
        }
    }

    // present 用のフレームグラフの構築
    RHI::FrameGraphDesc presentFrameGraphDesc{};
    presentFrameGraphDesc.usePresentQueue = true;
    presentFrameGraphDesc.enableProfiling = true;
    presentFrameGraphDesc.waitForCompletion = true;
    result = m_renderBackend->create_frame_graph(presentFrameGraphDesc, m_presentFrameGraph);
    if (!result)
    {
        return Result::fail(result.code, Severity::Fatal, "Failed to create present frame graph.");
    }

    // editorパスが提供されている場合は present グラフに追加
    if (a_editorPass)
    {
        m_presentFrameGraph->add_pass(std::move(a_editorPass));
    }
    else
    {
        m_presentFrameGraph->add_pass(std::make_unique<RHI::PresentToSwapChainPass>());
    }

    // グラフを構築
    result = m_presentFrameGraph->build();
    if (!result)
    {
        return result;
    }

    return Result::ok();
}

Result Engine::initialize_render_extraction_pipeline()
{
    ECS::ECSManager* ecs = nullptr;
    Result result = m_gameWorld.ecs(ecs);
    if (!result)
    {
        return result;
    }
    if (ecs == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Fatal, "GameWorld ECS is not initialized.");
    }

    // Runtime の GameWorld には pipeline を持たせず、Engine
    // 側で描画抽出順だけを定義する。
    ECS::CameraSystem& cameraSystem = ecs->add_system<ECS::CameraSystem>(m_gameWorld, m_drawFrameState, m_drawScenes);
    ECS::RenderableObjectSystem& renderableSystem = ecs->add_system<ECS::RenderableObjectSystem>(m_drawScenes);

    m_renderExtractionPipeline.clear();
    m_renderExtractionPipeline.add_system(&cameraSystem);
    m_renderExtractionPipeline.add_system(&renderableSystem);
    return Result::ok();
}

Result Engine::initialize_test_scene()
{
    if (m_meshPool == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error, "MeshPool is not initialized.");
    }

    RHI::MeshHandle cubeHandle{};
    Result result = m_meshPool->allocate_mesh(make_test_cube_mesh(), cubeHandle);
    if (!result)
    {
        return result;
    }

    uint32_t cubeMeshId = ECS::k_invalidMeshId;
    result = m_meshPool->get_mesh_id(cubeHandle, cubeMeshId);
    if (!result)
    {
        return result;
    }

    GameCore::GameObject cube{};
    result = m_gameWorld.create_static_mesh_object("TestCube", cubeMeshId, 0u, Math::float3(0.0f, 0.0f, 4.0f), cube);
    if (!result)
    {
        return result;
    }

    GameCore::GameObject camera{};
    result = m_gameWorld.create_object("MainCamera", camera);
    if (!result)
    {
        return result;
    }

    ECS::TransformComponent* cameraTransform = nullptr;
    result = m_gameWorld.add_component<ECS::TransformComponent>(camera.entity_id(), cameraTransform);
    if (!result)
    {
        return result;
    }

    ECS::WorldTransformComponent* cameraWorldTransform = nullptr;
    result = m_gameWorld.add_component<ECS::WorldTransformComponent>(camera.entity_id(), cameraWorldTransform);
    if (!result)
    {
        return result;
    }

    ECS::CameraComponent* cameraComponent = nullptr;
    result = m_gameWorld.add_component<ECS::CameraComponent>(camera.entity_id(), cameraComponent);
    if (!result)
    {
        return result;
    }

    // identity rotation の camera は +Z 方向を見る。cube は z=4
    // に置いて最小表示を成立させる。
    cameraTransform->position = Math::float3::zero();
    cameraWorldTransform->position = cameraTransform->position;
    cameraComponent->fovY = 60.0f;
    cameraComponent->aspectRatio = 0.0f;
    cameraComponent->nearZ = 0.1f;
    cameraComponent->farZ = 1000.0f;

    result = m_gameWorld.set_render_camera(camera.entity_id());
    if (!result)
    {
        return result;
    }

    m_gameWorld.sync_world_transforms();
    return Result::ok();
}

Result Engine::update_draw_scene(uint32_t a_bufferIndex)
{
    if (m_drawResources == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error, "DrawResources is not initialized.");
    }

    if (m_meshPool == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error, "MeshPool is not initialized.");
    }

    if (a_bufferIndex >= m_drawScenes.size())
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "DrawScene buffer index is out of range.");
    }

    DrawSystem::DrawScene& drawScene = m_drawScenes[a_bufferIndex];
    drawScene.clear();

    DrawSystem::DrawFrameData& frameData = m_drawFrameState.frame_state(a_bufferIndex);
    frameData.renderWidth = m_renderBackend->width();
    frameData.renderHeight = m_renderBackend->height();
    frameData.objectCount = 0;
    frameData.staticMeshBatches.clear();
    frameData.staticMeshIndirectCommands.clear();
    frameData.staticMeshObjectIndices.clear();
    frameData.staticMeshBatchCount = 0;
    frameData.indirectCommandCount = 0;
    frameData.useCpuBatching = false;

    // 最小動作確認用に local Transform を回し、描画抽出前に WorldTransform
    // へ同期する。
    constexpr float k_fixedDeltaTime = 1.0f / 60.0f;
    m_gameWorld.animate_static_mesh_objects(k_fixedDeltaTime);
    m_gameWorld.sync_world_transforms();

    ECS::ECSManager* ecs = nullptr;
    Result result = m_gameWorld.ecs(ecs);
    if (!result)
    {
        return result;
    }
    if (ecs == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Fatal, "GameWorld ECS is not initialized.");
    }

    const ECS::UpdateContext updateContext{a_bufferIndex, k_fixedDeltaTime};
    m_renderExtractionPipeline.update(*ecs, updateContext);

    if (m_hasRenderViewOverride)
    {
        // Editor など外部で確定した視点を使う場合、GameCore camera
        // 候補を置き換える。
        DrawSystem::CameraDrawItem camera{};
        camera.renderView = m_renderViewOverride;
        camera.renderView.width = frameData.renderWidth;
        camera.renderView.height = frameData.renderHeight;
        drawScene.clear_cameras();
        result = drawScene.add_camera(camera);
        if (!result)
        {
            return result;
        }
    }

    DrawSystem::StaticMeshBatchBuildResult batchBuildResult{};
    result = DrawSystem::StaticMeshBatcher::build_indirect_commands(drawScene, *m_meshPool, batchBuildResult);
    if (!result)
    {
        return result;
    }

    frameData.staticMeshBatches = std::move(batchBuildResult.batches);
    frameData.staticMeshIndirectCommands = std::move(batchBuildResult.commands);
    frameData.staticMeshObjectIndices = std::move(batchBuildResult.groupedObjectIndices);
    frameData.staticMeshBatchCount = static_cast<uint32_t>(frameData.staticMeshBatches.size());
    frameData.indirectCommandCount = static_cast<uint32_t>(frameData.staticMeshIndirectCommands.size());
    frameData.useCpuBatching = frameData.indirectCommandCount > 0;

    result = m_drawResources->upload_draw_scene(a_bufferIndex, drawScene, frameData);
    if (!result)
    {
        return result;
    }

    if (m_isDebugRenderingEnabled)
    {
        if (m_debugDrawResources == nullptr || m_debugRenderView == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "Debug rendering is enabled without resources.");
        }
        if (a_bufferIndex >= m_debugDrawScenes.size())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Debug DrawScene buffer index is out of range.");
        }

        // Debug 描画は Main と同じ描画対象を使い、camera だけ Editor DebugCamera に差し替える。
        DrawSystem::DrawScene& debugDrawScene = m_debugDrawScenes[a_bufferIndex];
        debugDrawScene = drawScene;
        debugDrawScene.clear_cameras();

        DrawSystem::CameraDrawItem debugCamera{};
        debugCamera.renderView = *m_debugRenderView;
        result = debugDrawScene.add_camera(debugCamera);
        if (!result)
        {
            return result;
        }

        DrawSystem::DrawFrameData& debugFrameData = m_debugDrawFrameState.frame_state(a_bufferIndex);
        debugFrameData = frameData;
        result = m_debugDrawResources->upload_draw_scene(a_bufferIndex, debugDrawScene, debugFrameData);
        if (!result)
        {
            return result;
        }
    }

    return Result::ok();
}

Result Engine::apply_pending_resize()
{
    PAL::PendingResizeRequest request;

    // プラットフォームランタイムステートから保留中のリサイズ要求を消費
    if (!m_platformRuntimeState.consume_pending_resize_request(request))
    {
        return Result::ok();
    }

    // サイズが変わらない場合は何もしない
    if (request.width == m_renderBackend->width() && request.height == m_renderBackend->height())
    {
        return Result::ok();
    }

    // GPU 処理の完了を待つ
    Result result = m_renderBackend->wait_for_idle();
    if (!result)
    {
        return result;
    }

    // 古いレンダーターゲットの破棄
    result = RHI::destroy_render_target_resources(*m_renderBackend, m_finalColorRenderTarget);
    if (!result)
    {
        return result;
    }
    result = RHI::destroy_render_target_resources(*m_renderBackend, m_debugColorRenderTarget);
    if (!result)
    {
        return result;
    }

    // バックエンドのリサイズ
    result = m_renderBackend->resize(request.width, request.height);
    if (!result)
    {
        return result;
    }

    // 新しいレンダーターゲットの作成
    result =
        RHI::create_render_target_resources(*m_renderBackend, "FinalColor", RHI::ColorFormat::R8G8B8A8_UNORM,
                                            m_finalColorRenderTarget, Math::float4::from_rgba8(63, 63, 63, 255).data());
    if (!result)
    {
        return result;
    }
    if (m_isDebugRenderingEnabled)
    {
        result = RHI::create_render_target_resources(*m_renderBackend, "DebugColor", RHI::ColorFormat::R8G8B8A8_UNORM,
                                                    m_debugColorRenderTarget,
                                                    Math::float4::from_rgba8(63, 63, 63, 255).data());
        if (!result)
        {
            return result;
        }
    }

    // フレームグラフの再構築
    result = m_frameGraph->rebuild(m_renderBackend->width(), m_renderBackend->height());
    if (!result)
    {
        return result;
    }
    if (m_debugFrameGraph != nullptr)
    {
        result = m_debugFrameGraph->rebuild(m_renderBackend->width(), m_renderBackend->height());
        if (!result)
        {
            return result;
        }
    }

    return m_presentFrameGraph->rebuild(m_renderBackend->width(), m_renderBackend->height());
}

void Engine::set_render_view_override(const DrawSystem::RenderView& a_renderView) noexcept
{
    m_renderViewOverride = a_renderView;
    m_hasRenderViewOverride = true;
}

void Engine::clear_render_view_override() noexcept
{
    m_hasRenderViewOverride = false;
}

} // namespace Cue
