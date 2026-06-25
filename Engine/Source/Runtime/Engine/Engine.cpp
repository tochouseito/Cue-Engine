#include "Engine.h"

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === Engine includes ===
#include "Command/PlatformCommandContext.h"
#include "GameCore/GameWorldRenderExtractor.h"

// === Frame Passes includes ===
#include "DrawSystem/passes/FinalColorClearPass.h"
#include "DrawSystem/passes/PresentToSwapChain.h"

// === C++ includes ===
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
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
        m_platform = a_info.platform;
        m_renderBackend = a_info.renderBackend;
        m_bufferCount = a_info.renderBackend->buffer_count();

        // FrameState の初期化
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
            desc, m_platform->thread_factory(), m_platform->clock(), m_platform->waiter(), update(), render(),
            present(),
            [this]()
            {
                Result r = apply_pending_resize();
                CUE_ASSERT_FORMAT(success(r), "Failed to apply pending resize: {}", r.message.data());
            });

        // 共有リソースの作成
        r = RHI::create_render_target_resources(*m_renderBackend, "FinalColor", RHI::ColorFormat::R8G8B8A8_UNORM,
                                                m_finalColorRenderTarget,
                                                Math::float4::from_rgba8(63, 63, 63, 255).data());
        if (!r)
        {
            return r;
        }

        if (a_info.editorPass)
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
        m_meshPool = std::make_unique<DrawSystem::MeshPool>(meshPoolDesc, *bufferManager, *viewManager, *commandPool,
                                                            *queuePool);

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
                CUE_ASSERT_FORMAT(false, "Failed to destroy final color render target: %s",
                                  destroyResult.message.data());
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
        m_frameGraph->add_pass(std::make_unique<DrawSystem::FinalColorClearPass>());

        // グラフを構築
        result = m_frameGraph->build();
        if (!result)
        {
            return result;
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

    Result Engine::update_draw_scene(uint32_t a_bufferIndex)
    {
        if (m_drawResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawResources is not initialized.");
        }

        if (a_bufferIndex >= m_drawScenes.size())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "DrawScene buffer index is out of range.");
        }

        DrawSystem::DrawScene& drawScene = m_drawScenes[a_bufferIndex];
        Result result = GameCore::GameWorldRenderExtractor::extract_static_mesh_draw_scene(m_gameWorld, drawScene);
        if (!result)
        {
            return result;
        }

        DrawSystem::DrawFrameData& frameData = m_drawFrameState.frame_state(a_bufferIndex);
        return m_drawResources->upload_draw_scene(a_bufferIndex, drawScene, frameData);
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

        // バックエンドのリサイズ
        result = m_renderBackend->resize(request.width, request.height);
        if (!result)
        {
            return result;
        }

        // 新しいレンダーターゲットの作成
        result = RHI::create_render_target_resources(*m_renderBackend, "FinalColor", RHI::ColorFormat::R8G8B8A8_UNORM,
                                                     m_finalColorRenderTarget,
                                                     Math::float4::from_rgba8(63, 63, 63, 255).data());
        if (!result)
        {
            return result;
        }

        // フレームグラフの再構築
        result = m_frameGraph->rebuild(m_renderBackend->width(), m_renderBackend->height());
        if (!result)
        {
            return result;
        }

        return m_presentFrameGraph->rebuild(m_renderBackend->width(), m_renderBackend->height());
    }

} // namespace Cue
