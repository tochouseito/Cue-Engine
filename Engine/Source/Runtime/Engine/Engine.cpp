#include "Engine.h"

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === Engine includes ===
#include "Command/EngineCommandContext.h"
#include "Command/PlatformCommandContext.h"
#include "DrawSystem/StaticMeshBatcher.h"
#include "GameCore/SceneWorldMapper.h"
#include "GameCore/TransformSystem.h"
#include "DrawSystem/Systems/CameraSystem.h"
#include "DrawSystem/Systems/RenderableObjectSystem.h"
#include "GameCore/SceneAsset.h"

// === Frame Passes includes ===
#include "DrawSystem/passes/DrawSceneUploadCopyPass.h"
#include "DrawSystem/passes/DrawViewUploadCopyPass.h"
#include "DrawSystem/passes/DrawVisibilityUploadCopyPass.h"
#include "DrawSystem/passes/PresentToSwapChain.h"
#include "DrawSystem/passes/StaticMeshIndirectPass.h"

// === C++ includes ===
#include <algorithm>
#include <cstdint>
#include <utility>

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
    m_gameCommandBridge = a_info.gameCommandBridge;
    m_platform = a_info.platform;
    m_renderBackend = a_info.renderBackend;
    m_bufferCount = a_info.renderBackend->buffer_count();
    m_debugRenderView = a_info.debugRenderView;
    // Editor pass と DebugCamera が揃う場合だけ Debug 用 GPU resource を確保し、Runtime の常駐メモリを増やさない
    m_isDebugRenderingEnabled = a_info.editorPass != nullptr && m_debugRenderView != nullptr;

    // Game と Debug は同じ Scene 抽出結果を使うため、CPU 側 DrawScene と batch 結果は frame ごとに一組だけ保持する
    m_drawFrameState.resize(m_bufferCount);
    m_drawScenes.resize(m_bufferCount);
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

    r = RHI::create_depth_stencil_resources(*m_renderBackend, "FinalDepth", m_finalDepthStencil);
    if (!r)
    {
        return r;
    }

    if (m_isDebugRenderingEnabled)
    {
        // DebugColor は Editor が SRV として読むため、DebugView を実際に描画する場合だけ作成する
        r = RHI::create_render_target_resources(*m_renderBackend, "DebugColor", RHI::ColorFormat::R8G8B8A8_UNORM,
                                                m_debugColorRenderTarget,
                                                Math::float4::from_rgba8(63, 63, 63, 255).data());
        if (!r)
        {
            return r;
        }

        r = RHI::create_depth_stencil_resources(*m_renderBackend, "DebugDepth", m_debugDepthStencil);
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
    r = m_meshPool->initialize_primitives();
    if (!r)
    {
        return r;
    }

    // Scene 入力は GameView と DebugView で共有し、camera と visibility だけを View ごとに分離する
    // 同じ最大 object 数を Scene と Visibility に使い、culling 結果が Scene object index を直接参照できるようにする
    m_drawSceneResources =
        std::make_unique<DrawSystem::DrawSceneResources>(bufferManager, viewManager, m_bufferCount);
    m_maxObjectCount = k_maxObjectCount;
    m_maxCellCount = (m_maxObjectCount + k_cellObjectCapacity - 1u) / k_cellObjectCapacity;

    r = m_drawSceneResources->initialize(m_maxObjectCount, m_maxCellCount);
    if (!r)
    {
        return r;
    }

    m_gameViewResources =
        std::make_unique<DrawSystem::DrawViewResources>(bufferManager, m_bufferCount, "Game");
    r = m_gameViewResources->initialize();
    if (!r)
    {
        return r;
    }

    // GameView は Editor の有無に関係なく描画するため、View と Visibility resource を常に確保する
    m_gameVisibilityResources = std::make_unique<DrawSystem::DrawVisibilityResources>(
        bufferManager, viewManager, m_bufferCount, "Game");
    r = m_gameVisibilityResources->initialize(m_maxObjectCount, m_maxObjectCount, m_maxObjectCount);
    if (!r)
    {
        return r;
    }

    if (m_isDebugRenderingEnabled)
    {
        // DebugView は camera と可視集合だけを GameView から分離し、shared Scene buffer を再確保しない
        m_debugViewResources =
            std::make_unique<DrawSystem::DrawViewResources>(bufferManager, m_bufferCount, "Debug");
        r = m_debugViewResources->initialize();
        if (!r)
        {
            return r;
        }

        m_debugVisibilityResources = std::make_unique<DrawSystem::DrawVisibilityResources>(
            bufferManager, viewManager, m_bufferCount, "Debug");
        r = m_debugVisibilityResources->initialize(m_maxObjectCount, m_maxObjectCount, m_maxObjectCount);
        if (!r)
        {
            return r;
        }
    }

    r = initialize_render_extraction_pipeline(
        m_gameWorld, m_gameRenderCameraSelection, m_renderExtractionPipeline);
    if (!r)
    {
        return r;
    }

    r = initialize_render_extraction_pipeline(
        m_runtimeGameWorld, m_runtimeRenderCameraSelection, m_runtimeRenderExtractionPipeline);
    if (!r)
    {
        return r;
    }

    r = initialize_default_camera();
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

        destroyResult = RHI::destroy_depth_stencil_resources(*m_renderBackend, m_finalDepthStencil);
        if (!destroyResult)
        {
            CUE_ASSERT_FORMAT(false, "Failed to destroy final depth stencil: %s", destroyResult.message.data());
        }

        destroyResult = RHI::destroy_depth_stencil_resources(*m_renderBackend, m_debugDepthStencil);
        if (!destroyResult)
        {
            CUE_ASSERT_FORMAT(false, "Failed to destroy debug depth stencil: %s", destroyResult.message.data());
        }
    }

    // 依存オブジェクトの解放
    m_platformCommandBridge = nullptr;
    m_gameCommandBridge = nullptr;
}

void Engine::set_asset_root_path(const Core::IO::Path& a_assetRootPath) noexcept
{
    m_assetRootPath = a_assetRootPath.normalize();
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
    if (m_gameCommandBridge)
    {
        EngineCommandContext gameCommandContext(m_gameWorld, m_gameRenderCameraSelection);
        Result result = m_gameCommandBridge->drain_commands(gameCommandContext);
        if (!result)
        {
            return result;
        }
    }

    return apply_pending_play_request();
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

Result Engine::request_start_play()
{
    if (m_playState != PlayState::editing || m_pendingPlayRequest != PlayRequest::none)
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "Play mode is already requested or active.");
    }

    m_pendingPlayRequest = PlayRequest::start;
    return Result::ok();
}

Result Engine::request_pause_play()
{
    if (m_playState != PlayState::playing || m_pendingPlayRequest != PlayRequest::none)
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "Play mode is not running.");
    }

    m_pendingPlayRequest = PlayRequest::pause;
    return Result::ok();
}

Result Engine::request_resume_play()
{
    if (m_playState != PlayState::paused || m_pendingPlayRequest != PlayRequest::none)
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "Play mode is not paused.");
    }

    m_pendingPlayRequest = PlayRequest::resume;
    return Result::ok();
}

Result Engine::request_step_play()
{
    if (m_playState != PlayState::paused || m_pendingPlayRequest != PlayRequest::none)
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "Play mode is not paused.");
    }

    m_pendingPlayRequest = PlayRequest::step;
    return Result::ok();
}

Result Engine::request_stop_play()
{
    if (m_playState == PlayState::editing || m_pendingPlayRequest != PlayRequest::none)
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "Play mode is not active.");
    }

    m_pendingPlayRequest = PlayRequest::stop;
    return Result::ok();
}

bool Engine::is_playing() const noexcept
{
    return m_playState != PlayState::editing;
}

bool Engine::is_play_paused() const noexcept
{
    return m_playState == PlayState::paused;
}

std::function<void(uint64_t, uint32_t)> Engine::update()
{
    return [this](uint64_t a_frameNo, uint32_t a_index)
    {
        (void)a_frameNo;

        Result result = update_draw_scene(a_index);
        CUE_ASSERT_FORMAT(success(result), "Failed to update draw scene: %s", result.message.data());
    };
}

std::function<void(uint64_t, uint32_t)> Engine::render()
{
    return [this](uint64_t a_frameNo, uint32_t a_index)
    {
        if (m_renderBackend != nullptr && m_frameGraph != nullptr)
        {
            // Main graph が失敗すると FinalColor が更新されず、Editor 側では空の View に見えるため失敗を伝播させます
            Result result = m_renderBackend->render(a_frameNo, a_index, *m_frameGraph);
            CUE_ASSERT_FORMAT(success(result), "Failed to render main frame graph: %s", result.message.data());
        }
        if (m_renderBackend != nullptr && m_debugFrameGraph != nullptr)
        {
            // Debug graph は Main と別の command context を使うため、失敗しても Main の結果から原因を推測できません
            Result result = m_renderBackend->render(a_frameNo, a_index, *m_debugFrameGraph);
            CUE_ASSERT_FORMAT(success(result), "Failed to render debug frame graph: %s", result.message.data());
        }
    };
}

std::function<void(uint64_t, uint32_t)> Engine::present()
{
    return [this](uint64_t a_frameNo, uint32_t a_index)
    {
        if (m_renderBackend != nullptr && m_presentFrameGraph != nullptr)
        {
            // Present graph の失敗は ImGui が SRV を読む遷移失敗にもなるため、swap chain 更新失敗として隠しません
            Result result = m_renderBackend->present(a_frameNo, a_index, false, *m_presentFrameGraph);
            CUE_ASSERT_FORMAT(success(result), "Failed to present frame graph: %s", result.message.data());
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

    if (m_drawSceneResources == nullptr || m_gameViewResources == nullptr ||
        m_gameVisibilityResources == nullptr || m_meshPool == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Fatal, "Game draw resources are not initialized.");
    }

    // Main は shared Scene 入力と GameView 固有の camera / visibility を組み合わせて描画する
    m_frameGraph->add_pass(std::make_unique<DrawSystem::DrawSceneUploadCopyPass>(*m_drawSceneResources));
    m_frameGraph->add_pass(std::make_unique<DrawSystem::DrawViewUploadCopyPass>(*m_gameViewResources));
    m_frameGraph->add_pass(
        std::make_unique<DrawSystem::DrawVisibilityUploadCopyPass>(*m_gameVisibilityResources));
    m_frameGraph->add_pass(
        std::make_unique<DrawSystem::StaticMeshIndirectPass>(
            *m_drawSceneResources,
            *m_gameViewResources,
            *m_gameVisibilityResources,
            *m_meshPool,
            m_drawFrameState));

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
        if (m_debugViewResources == nullptr || m_debugVisibilityResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Fatal, "Debug view resources are not initialized.");
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

        m_debugFrameGraph->add_pass(std::make_unique<DrawSystem::DrawViewUploadCopyPass>(*m_debugViewResources));
        m_debugFrameGraph->add_pass(
            std::make_unique<DrawSystem::DrawVisibilityUploadCopyPass>(*m_debugVisibilityResources));
        m_debugFrameGraph->add_pass(
            std::make_unique<DrawSystem::StaticMeshIndirectPass>(
                *m_drawSceneResources,
                *m_debugViewResources,
                *m_debugVisibilityResources,
                *m_meshPool,
                m_drawFrameState,
                "DebugStaticMeshIndirect",
                "DebugColor",
                "DebugDepth"));

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

Result Engine::initialize_render_extraction_pipeline(
    GameCore::GameWorld& a_world,
    const DrawSystem::RenderCameraSelection& a_cameraSelection,
    ECS::ECSManager::SystemPipeline& a_outPipeline)
{
    ECS::ECSManager* ecs = nullptr;
    Result result = a_world.ecs(ecs);
    if (!result)
    {
        return result;
    }
    if (ecs == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Fatal, "GameWorld ECS is not initialized.");
    }

    // GameWorld は描画層へ依存させず、Engine 側で World ごとの描画抽出順だけを定義する
    ECS::CameraSystem& cameraSystem =
        ecs->add_system<ECS::CameraSystem>(a_world, a_cameraSelection, m_drawFrameState, m_drawScenes);
    ECS::RenderableObjectSystem& renderableSystem = ecs->add_system<ECS::RenderableObjectSystem>(m_drawScenes);

    a_outPipeline.clear();
    a_outPipeline.add_system(&cameraSystem);
    a_outPipeline.add_system(&renderableSystem);
    return Result::ok();
}

Result Engine::initialize_default_camera()
{
    GameCore::GameObject camera{};
    Result result = m_gameWorld.create_object("MainCamera", camera);
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

    // 初期 Scene が空でも GameView の描画視点だけは成立させる
    cameraTransform->position = Math::float3::zero();
    cameraWorldTransform->position = cameraTransform->position;
    cameraComponent->fovY = 60.0f;
    cameraComponent->aspectRatio = 0.0f;
    cameraComponent->nearZ = 0.1f;
    cameraComponent->farZ = 1000.0f;

    m_gameRenderCameraSelection.set_camera_entity(camera.entity_id());
    GameCore::TransformSystem::sync_world_transforms(m_gameWorld);
    return Result::ok();
}

Result Engine::update_draw_scene(uint32_t a_bufferIndex)
{
    if (m_drawSceneResources == nullptr || m_gameViewResources == nullptr ||
        m_gameVisibilityResources == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error, "Game draw resources are not initialized.");
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

    GameCore::GameWorld& activeWorld = active_game_world();
    ECS::ECSManager::SystemPipeline& activePipeline = active_render_extraction_pipeline();

    // runtime World の固定更新要求は GameScript System が消費する。Editor World は authoring 状態のまま保つ。
    constexpr float k_fixedDeltaTime = 1.0f / 60.0f;
    if (m_playState == PlayState::playing || m_isPlayStepRequested)
    {
        m_isPlayStepRequested = false;
    }

    // 描画抽出は WorldTransform を参照するため、階層変換を先に確定する
    GameCore::TransformSystem::sync_world_transforms(activeWorld);

    ECS::ECSManager* ecs = nullptr;
    Result result = activeWorld.ecs(ecs);
    if (!result)
    {
        return result;
    }
    if (ecs == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Fatal, "GameWorld ECS is not initialized.");
    }

    const ECS::UpdateContext updateContext{a_bufferIndex, k_fixedDeltaTime};
    activePipeline.update(*ecs, updateContext);

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

    result = m_drawSceneResources->upload_draw_scene(a_bufferIndex, drawScene);
    if (!result)
    {
        return result;
    }

    // GameView は GameCore が選択した camera を使い、camera が無い frame では既定値を upload する
    DrawSystem::RenderView gameRenderView{};
    const std::vector<DrawSystem::CameraDrawItem>& gameCameras = drawScene.cameras();
    if (!gameCameras.empty())
    {
        gameRenderView = gameCameras.front().renderView;
    }
    result = m_gameViewResources->upload_view(a_bufferIndex, gameRenderView);
    if (!result)
    {
        return result;
    }

    result = m_gameVisibilityResources->upload_visibility(a_bufferIndex, frameData);
    if (!result)
    {
        return result;
    }

    if (m_isDebugRenderingEnabled)
    {
        if (m_debugViewResources == nullptr || m_debugVisibilityResources == nullptr ||
            m_debugRenderView == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "Debug view resources are not initialized.");
        }

        // DebugView は shared Scene 入力を再 upload せず、Editor camera と visibility だけを差し替える
        result = m_debugViewResources->upload_view(a_bufferIndex, *m_debugRenderView);
        if (!result)
        {
            return result;
        }
        result = m_debugVisibilityResources->upload_visibility(a_bufferIndex, frameData);
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
    result = RHI::destroy_depth_stencil_resources(*m_renderBackend, m_finalDepthStencil);
    if (!result)
    {
        return result;
    }
    result = RHI::destroy_depth_stencil_resources(*m_renderBackend, m_debugDepthStencil);
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
    result = RHI::create_depth_stencil_resources(*m_renderBackend, "FinalDepth", m_finalDepthStencil);
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
        result = RHI::create_depth_stencil_resources(*m_renderBackend, "DebugDepth", m_debugDepthStencil);
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

Result Engine::apply_pending_play_request()
{
    const PlayRequest request = m_pendingPlayRequest;
    m_pendingPlayRequest = PlayRequest::none;

    switch (request)
    {
    case PlayRequest::none:
        return Result::ok();
    case PlayRequest::start:
        return start_play();
    case PlayRequest::pause:
        m_playState = PlayState::paused;
        return Result::ok();
    case PlayRequest::resume:
        m_playState = PlayState::playing;
        return Result::ok();
    case PlayRequest::step:
        m_isPlayStepRequested = true;
        return Result::ok();
    case PlayRequest::stop:
        return stop_play();
    default:
        return Result::fail(Code::InvalidArgument, Severity::Error, "Unknown play request.");
    }
}

Result Engine::start_play()
{
    GameCore::SceneAsset runtimeScene{};
    Result result = GameCore::SceneWorldMapper::make_asset(m_gameWorld, "Runtime", runtimeScene);
    if (!result)
    {
        return result;
    }

    m_runtimeRenderCameraSelection.clear();
    GameCore::EntityId runtimeCameraEntity = GameCore::k_invalidEntityId;
    result = GameCore::SceneWorldMapper::load_into(
        m_runtimeGameWorld, runtimeScene, runtimeCameraEntity);
    if (!result)
    {
        return result;
    }

    if (runtimeCameraEntity != GameCore::k_invalidEntityId)
    {
        m_runtimeRenderCameraSelection.set_camera_entity(runtimeCameraEntity);
    }

    m_playState = PlayState::playing;
    m_isPlayStepRequested = false;
    return Result::ok();
}

Result Engine::stop_play()
{
    // 描画側が runtime World を参照しない状態に戻してから、実行中の Entity を破棄する
    m_playState = PlayState::editing;
    m_isPlayStepRequested = false;
    m_runtimeRenderCameraSelection.clear();
    return m_runtimeGameWorld.clear();
}

GameCore::GameWorld& Engine::active_game_world() noexcept
{
    return m_playState == PlayState::editing ? m_gameWorld : m_runtimeGameWorld;
}

ECS::ECSManager::SystemPipeline& Engine::active_render_extraction_pipeline() noexcept
{
    return m_playState == PlayState::editing
               ? m_renderExtractionPipeline
               : m_runtimeRenderExtractionPipeline;
}

} // namespace Cue
