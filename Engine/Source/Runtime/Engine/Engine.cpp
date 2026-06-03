#include "Engine.h"

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === Engine includes ===
#include "Command/PlatformCommandContext.h"

// === Frame Passes includes ===
#include "DrawSystem/passes/DrawResourceCopyPasses.h"
#include "DrawSystem/passes/BuildHiZDepthPass.h"
#include "DrawSystem/passes/FinalColorClearPass.h"
#include "DrawSystem/passes/GenerateVisibleListPass.h"
#include "DrawSystem/passes/MeshForwardPass.h"
#include "DrawSystem/passes/ObjectOcclusionDepthPass.h"
#include "DrawSystem/passes/PresentToSwapChain.h"
#include "DrawSystem/passes/StaticMeshBatchingPass.h"
#include "DrawSystem/passes/StaticMeshForwardPass.h"
#include "LightingSystem/Passes/LightBufferCopyPass.h"

// === C++ includes ===
#include <algorithm>
#include <cstdint>
#include <iterator>

namespace Cue
{
namespace
{
constexpr float k_pi = 3.14159265358979323846f;
constexpr uint32_t k_maxObjectCount = 50000;

[[nodiscard]] uint64_t instance_count(Math::uint3 a_instanceCounts) noexcept
{
    return static_cast<uint64_t>(a_instanceCounts.x) *
           static_cast<uint64_t>(a_instanceCounts.y) *
           static_cast<uint64_t>(a_instanceCounts.z);
}

[[nodiscard]] uint32_t grid_index(
    Math::uint3 instanceCounts, uint32_t x, uint32_t y, uint32_t z) noexcept
{
    return (z * instanceCounts.y + y) * instanceCounts.x + x;
}

[[nodiscard]] Math::float4 point_light_color(uint32_t lightIndex) noexcept
{
    constexpr Math::float4 k_colors[] = {
        Math::float4(1.0f, 0.55f, 0.28f, 1.0f),
        Math::float4(0.32f, 0.72f, 1.0f, 1.0f),
        Math::float4(0.74f, 0.55f, 1.0f, 1.0f),
        Math::float4(0.55f, 1.0f, 0.68f, 1.0f),
    };
    return k_colors[lightIndex % std::size(k_colors)];
}
} // namespace

Result Engine::initialize(EngineSetupInfo& a_info)
{
    Result r = Result::ok();

    // 引数の検査
    if (a_info.platformCommandBridge == nullptr)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "Platform command bridge must not be null.");
    }

    // 依存オブジェクトの保存
    m_platformCommandBridge = a_info.platformCommandBridge;
    m_platform = a_info.platform;
    m_renderBackend = a_info.renderBackend;
    m_bufferCount = a_info.renderBackend->buffer_count();
    m_maxPointLightCount = a_info.maxPointLightCount;
    m_pointLightBufferCapacity = std::max(1u, m_maxPointLightCount);
    m_drawFrameState.resize(m_bufferCount);
    for (uint32_t frameIndex = 0; frameIndex < m_bufferCount; ++frameIndex)
    {
        DrawSystem::DrawFrameData& frameState =
            m_drawFrameState.frame_state(frameIndex);
        frameState.renderWidth = 1280;
        frameState.renderHeight = 720;
        frameState.objectCount = 0;
    }

    // フレームコントローラーの生成
    FrameControllerDesc desc(m_bufferCount);
    desc.mode = ControllerMode::Backpressure;
    desc.maxFps = a_info.maxFps;
    m_frameController = std::make_unique<FrameController>(
      desc, m_platform->thread_factory(), m_platform->clock(),
      m_platform->waiter(), update(), render(), present(), [this]() {

      });

    // 共有リソースの作成
    r = RHI::create_render_target_resources(*m_renderBackend, "FinalColor",
                                            RHI::ColorFormat::R8G8B8A8_UNORM,
                                            m_gameRenderTarget);
    if (!r)
    {
        return r;
    }

    auto* bufferManager = m_renderBackend->get_buffer_manager();
    if (bufferManager == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Fatal,
                            "Failed to get buffer manager from backend.");
    }

    auto* viewManager = m_renderBackend->get_view_manager();
    if (viewManager == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Fatal,
                            "Failed to get view manager from backend.");
    }

    auto* commandPool = m_renderBackend->get_command_pool();
    auto* queuePool = m_renderBackend->get_queue_pool();
    if (commandPool == nullptr || queuePool == nullptr)
    {
        return Result::fail(
            Code::NotFound, Severity::Fatal,
            "Failed to get command or queue pool from backend.");
    }

    // MeshPool の生成
    DrawSystem::MeshPoolDesc meshPoolDesc{};
    m_meshPool = std::make_unique<DrawSystem::MeshPool>(
        meshPoolDesc, *bufferManager, *viewManager, *commandPool, *queuePool);

    m_drawResources = std::make_unique<DrawSystem::DrawResources>(
        bufferManager, viewManager, m_bufferCount);
    m_maxObjectCount = k_maxObjectCount;

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

    m_lightResources = std::make_unique<LightingSystem::LightResources>(
        bufferManager, viewManager, m_bufferCount);
    r = m_lightResources->create_frame_buffer();
    if (!r)
    {
        return r;
    }
    r = m_lightResources->create_point_light_buffer(m_pointLightBufferCapacity);
    if (!r)
    {
        return r;
    }

    m_material.color = Math::float4(0.72f, 0.68f, 0.58f, 1.0f);
    m_material.shininess = 32.0f;
    m_lightFrame.ambientColorIntensity = Math::float4(1.0f, 1.0f, 1.0f, 0.16f);
    m_viewProjection.view = Math::float4x4::identity();
    m_viewProjection.projection = Math::perspective_fov_matrix(
        60.0f * k_pi / 180.0f, 1280.0f / 720.0f, 0.01f, 100.0f);
    m_viewProjection.cameraPosition = Math::float4(0.0f, 0.0f, -5.0f, 1.0f);
    r = commit_static_draw_data_to_uploaders();
    if (!r)
    {
        return r;
    }
    r = commit_view_projection_to_uploaders();
    if (!r)
    {
        return r;
    }
    r = commit_light_data_to_uploaders();
    if (!r)
    {
        return r;
    }

    // FrameGraph の生成
    r = create_frame_graphs(nullptr);
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
            CUE_ASSERT_FORMAT(false,
                              "Failed to wait backend idle during shutdown: %s",
                              waitResult.message.data());
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
        PlatformCommandContext platformCommandContext(m_platformRuntimeState);
        Result result =
            m_platformCommandBridge->drain_commands(platformCommandContext);
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

Result Engine::register_model(const Core::Native::ModelData& a_modelData)
{
    return register_model(a_modelData, Math::uint3(1u, 1u, 1u), 0.0f);
}

Result Engine::register_model(const Core::Native::ModelData& a_modelData,
                              Math::uint3 a_instanceCounts)
{
    return register_model(a_modelData, a_instanceCounts, 0.0f);
}

Result Engine::register_model(const Core::Native::ModelData& a_modelData,
                              Math::uint3 a_instanceCounts,
                              float a_targetRadius)
{
    if (m_meshPool == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "MeshPool is not initialized.");
    }
    if (a_modelData.meshes.empty())
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "ModelData does not contain any mesh.");
    }
    if (a_instanceCounts.x == 0 || a_instanceCounts.y == 0 ||
        a_instanceCounts.z == 0)
    {
        return Result::fail(
            Code::InvalidArgument, Severity::Error,
            "Dragon instance counts must be greater than zero.");
    }

    const uint64_t totalInstanceCount = instance_count(a_instanceCounts);
    if (totalInstanceCount > m_maxObjectCount)
    {
        return Result::fail(
            Code::InvalidArgument, Severity::Error,
            "Dragon instance count exceeds DrawResources capacity.");
    }

    const size_t firstHandleIndex = m_meshHandles.size();
    m_meshHandles.reserve(m_meshHandles.size() + a_modelData.meshes.size());
    for (const Core::Native::MeshData& meshData : a_modelData.meshes)
    {
        RHI::MeshHandle meshHandle{};
        Result result = m_meshPool->allocate_mesh(meshData, meshHandle);
        if (!result)
        {
            return result;
        }

        m_meshHandles.push_back(meshHandle);
    }

    uint32_t drawMeshIndex = 0;
    std::vector<uint32_t> drawLodMeshIndices{};
    if (!a_modelData.renderParts.empty())
    {
        drawMeshIndex = a_modelData.renderParts[0].meshIndex;
        drawLodMeshIndices = a_modelData.renderParts[0].lodMeshIndices;
    }
    if (drawLodMeshIndices.empty())
    {
        drawLodMeshIndices.push_back(drawMeshIndex);
    }
    if (drawMeshIndex >= a_modelData.meshes.size())
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "Model render part mesh index is out of range.");
    }

    m_drawLodMeshIds = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    m_drawLodCount = std::min<uint32_t>(
        4u, static_cast<uint32_t>(drawLodMeshIndices.size()));
    for (uint32_t lodIndex = 0; lodIndex < m_drawLodCount; ++lodIndex)
    {
        const uint32_t lodMeshIndex = drawLodMeshIndices[lodIndex];
        if (lodMeshIndex >= a_modelData.meshes.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Model render part LOD mesh index is out of range.");
        }

        RHI::MeshHandle lodMeshHandle =
            m_meshHandles[firstHandleIndex + lodMeshIndex];
        Result meshIdResult =
            m_meshPool->get_mesh_id(lodMeshHandle, m_drawLodMeshIds[lodIndex]);
        if (!meshIdResult)
        {
            return meshIdResult;
        }
    }
    m_drawMeshId = m_drawLodMeshIds[0];

    uint32_t materialIndex = Core::Native::k_invalidModelMaterialIndex;
    if (!a_modelData.renderParts.empty())
    {
        materialIndex = a_modelData.renderParts[0].materialIndex;
    }
    if (materialIndex < a_modelData.materials.size())
    {
        const Core::Native::ImportedMaterialData& importedMaterial =
            a_modelData.materials[materialIndex];
        m_material.color = importedMaterial.color;
        m_material.shininess = importedMaterial.shininess;
        m_material.useTexture = 0;
        m_material.textureId = 0;
        m_material.useReflectionSkybox = 0;
    }

    DrawSystem::MeshBounds bounds{};
    Result boundsResult = m_meshPool->get_mesh_bounds(m_drawMeshId, bounds);
    if (!boundsResult)
    {
        return boundsResult;
    }

    const float modelScale =
        a_targetRadius > 0.0f && bounds.radius > 0.0f
        ? a_targetRadius / bounds.radius
        : 1.0f;
    const float scaledRadius = bounds.radius * modelScale;
    const Math::float3 scaledBoundsCenter =
        bounds.center * modelScale;

    m_renderableInfos.clear();
    m_objectTransforms.clear();
    m_renderableInfos.reserve(static_cast<size_t>(totalInstanceCount));
    m_objectTransforms.reserve(static_cast<size_t>(totalInstanceCount));
    std::vector<Math::float3> objectPositions{};
    objectPositions.reserve(static_cast<size_t>(totalInstanceCount));

    const float spacing = std::max(scaledRadius * 2.5f, 0.75f);
    const Math::float3 gridOrigin(
        -0.5f * spacing * static_cast<float>(a_instanceCounts.x - 1u),
        -0.5f * spacing * static_cast<float>(a_instanceCounts.y - 1u),
        -0.5f * spacing * static_cast<float>(a_instanceCounts.z - 1u));

    for (uint32_t z = 0; z < a_instanceCounts.z; ++z)
    {
        for (uint32_t y = 0; y < a_instanceCounts.y; ++y)
        {
            for (uint32_t x = 0; x < a_instanceCounts.x; ++x)
            {
                const uint32_t objectId =
                    static_cast<uint32_t>(m_renderableInfos.size());

                GpuData::RenderableInfo renderableInfo{};
                renderableInfo.objectId = objectId;
                renderableInfo.visible = 1u;
                renderableInfo.meshId = m_drawMeshId;
                renderableInfo.transformId = objectId;
                renderableInfo.materialId = 0u;
                renderableInfo.lodMeshId0 = m_drawLodMeshIds[0];
                renderableInfo.lodMeshId1 = m_drawLodMeshIds[1];
                renderableInfo.lodMeshId2 = m_drawLodMeshIds[2];
                renderableInfo.lodMeshId3 = m_drawLodMeshIds[3];
                renderableInfo.lodCount = m_drawLodCount;

                const Math::float3 position(
                    gridOrigin.x + spacing * static_cast<float>(x),
                    gridOrigin.y + spacing * static_cast<float>(y),
                    gridOrigin.z + spacing * static_cast<float>(z));
                renderableInfo.boundsCenterRadius = Math::float4(
                    position.x + scaledBoundsCenter.x,
                    position.y + scaledBoundsCenter.y,
                    position.z + scaledBoundsCenter.z,
                    scaledRadius);
                m_renderableInfos.push_back(renderableInfo);

                GpuData::ObjectTransformGpu transform{};
                transform.worldMatrix =
                    Math::scale_matrix(Math::float3(
                        modelScale,
                        modelScale,
                        modelScale)) *
                    Math::translate_matrix(position);
                transform.normalMatrix = Math::float4x4::identity();
                m_objectTransforms.push_back(transform);
                objectPositions.push_back(position);
            }
        }
    }

    std::vector<Math::float3> pointLightCandidates{};
    pointLightCandidates.reserve(m_objectTransforms.size() * 3u);
    const auto add_midpoint = [&](uint32_t a, uint32_t b)
    {
        const Math::float3& first = objectPositions[a];
        const Math::float3& second = objectPositions[b];
        pointLightCandidates.emplace_back(
            (first.x + second.x) * 0.5f,
            (first.y + second.y) * 0.5f,
            (first.z + second.z) * 0.5f);
    };

    for (uint32_t z = 0; z < a_instanceCounts.z; ++z)
    {
        for (uint32_t y = 0; y < a_instanceCounts.y; ++y)
        {
            for (uint32_t x = 0; x < a_instanceCounts.x; ++x)
            {
                const uint32_t current =
                    grid_index(a_instanceCounts, x, y, z);
                if (x + 1u < a_instanceCounts.x)
                {
                    add_midpoint(
                        current, grid_index(a_instanceCounts, x + 1u, y, z));
                }
                if (y + 1u < a_instanceCounts.y)
                {
                    add_midpoint(
                        current, grid_index(a_instanceCounts, x, y + 1u, z));
                }
                if (z + 1u < a_instanceCounts.z)
                {
                    add_midpoint(
                        current, grid_index(a_instanceCounts, x, y, z + 1u));
                }
            }
        }
    }

    m_pointLights.clear();
    const uint32_t pointLightCount = std::min(
        m_maxPointLightCount,
        static_cast<uint32_t>(pointLightCandidates.size()));
    m_pointLights.reserve(pointLightCount);
    for (uint32_t lightIndex = 0; lightIndex < pointLightCount; ++lightIndex)
    {
        const uint32_t candidateIndex =
            static_cast<uint32_t>(
                (static_cast<uint64_t>(lightIndex) *
                 pointLightCandidates.size()) /
                pointLightCount);
        Math::float3 position = pointLightCandidates[candidateIndex];
        position.y += scaledBoundsCenter.y + scaledRadius * 0.55f;

        GpuData::PointLightGpu pointLight{};
        pointLight.positionRange =
            Math::float4(position.x, position.y, position.z, spacing * 2.6f);

        Math::float4 color = point_light_color(lightIndex);
        color.w = 1.8f;
        pointLight.colorIntensity = color;
        m_pointLights.push_back(pointLight);
    }
    m_lightFrame.pointLightCount = static_cast<uint32_t>(m_pointLights.size());

    m_drawObjectCount = static_cast<uint32_t>(m_renderableInfos.size());
    m_hasDrawableObject = true;
    for (uint32_t frameIndex = 0; frameIndex < m_bufferCount; ++frameIndex)
    {
        m_drawFrameState.frame_state(frameIndex).objectCount =
            m_drawObjectCount;
    }

    Result commitResult = commit_static_draw_data_to_uploaders();
    if (!commitResult)
    {
        return commitResult;
    }
    return commit_light_data_to_uploaders();
}

Result Engine::set_view_projection(
    const GpuData::ViewProjectionGpu& a_viewProjection)
{
    m_viewProjection = a_viewProjection;
    return commit_view_projection_to_uploaders();
}

Result Engine::commit_static_draw_data_to_uploaders()
{
    if (m_drawResources == nullptr)
    {
        return Result::ok();
    }

    for (uint32_t frameIndex = 0; frameIndex < m_bufferCount; ++frameIndex)
    {
        auto& renderableUploader =
            m_drawResources->renderable_info_uploaders()[frameIndex];
        renderableUploader.begin_frame();
        for (uint32_t objectIndex = 0; objectIndex < m_drawObjectCount;
             ++objectIndex)
        {
            if (!renderableUploader.push(objectIndex,
                                         m_renderableInfos[objectIndex]))
            {
                return Result::fail(Code::InternalError, Severity::Error,
                                    "Failed to push RenderableInfo uploader.");
            }
        }
        if (!renderableUploader.commit())
        {
            return Result::fail(Code::InternalError, Severity::Error,
                                "Failed to commit RenderableInfo uploader.");
        }

        auto& transformUploader =
            m_drawResources->transform_uploaders()[frameIndex];
        transformUploader.begin_frame();
        for (uint32_t objectIndex = 0; objectIndex < m_drawObjectCount;
             ++objectIndex)
        {
            if (!transformUploader.push(objectIndex,
                                        m_objectTransforms[objectIndex]))
            {
                return Result::fail(Code::InternalError, Severity::Error,
                                    "Failed to push Transform uploader.");
            }
        }
        if (!transformUploader.commit())
        {
            return Result::fail(Code::InternalError, Severity::Error,
                                "Failed to commit Transform uploader.");
        }

        auto& materialUploader =
            m_drawResources->material_uploaders()[frameIndex];
        materialUploader.begin_frame();
        if (!materialUploader.push(0, m_material) || !materialUploader.commit())
        {
            return Result::fail(Code::InternalError, Severity::Error,
                                "Failed to commit Material uploader.");
        }
    }

    return Result::ok();
}

Result Engine::commit_view_projection_to_uploaders()
{
    if (m_drawResources == nullptr)
    {
        return Result::ok();
    }

    for (uint32_t frameIndex = 0; frameIndex < m_bufferCount; ++frameIndex)
    {
        auto& uploader =
            m_drawResources->view_projection_uploaders()[frameIndex];
        uploader.begin_frame();
        if (!uploader.push(0, m_viewProjection) || !uploader.commit())
        {
            return Result::fail(Code::InternalError, Severity::Error,
                                "Failed to commit ViewProjection uploader.");
        }
    }

    return Result::ok();
}

Result Engine::commit_light_data_to_uploaders()
{
    if (m_lightResources == nullptr)
    {
        return Result::ok();
    }

    m_lightFrame.pointLightCount = static_cast<uint32_t>(m_pointLights.size());
    for (uint32_t frameIndex = 0; frameIndex < m_bufferCount; ++frameIndex)
    {
        auto& frameUploader = m_lightResources->frame_uploaders()[frameIndex];
        frameUploader.begin_frame();
        if (!frameUploader.push(0, m_lightFrame) || !frameUploader.commit())
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to commit LightFrame uploader.");
        }

        auto& pointLightUploader =
            m_lightResources->point_light_uploaders()[frameIndex];
        pointLightUploader.begin_frame();
        for (uint32_t lightIndex = 0; lightIndex < m_pointLights.size();
             ++lightIndex)
        {
            if (!pointLightUploader.push(lightIndex, m_pointLights[lightIndex]))
            {
                return Result::fail(
                    Code::InternalError,
                    Severity::Error,
                    "Failed to push PointLight uploader.");
            }
        }
        if (!pointLightUploader.commit())
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "Failed to commit PointLight uploader.");
        }
    }

    return Result::ok();
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
        m_renderBackend->present(a_frameNo, a_index, true,
                                 *m_presentFrameGraph);
    };
}

Result Engine::create_frame_graphs(
    std::unique_ptr<RHI::FrameGraphPass> a_editorPass)
{
    Result result = Result::ok();

    RHI::FrameGraphDesc renderFrameGraphDesc{};
    renderFrameGraphDesc.usePresentQueue = true;
    renderFrameGraphDesc.enableProfiling = true;
    renderFrameGraphDesc.waitForCompletion = false;
    result =
        m_renderBackend->create_frame_graph(renderFrameGraphDesc, m_frameGraph);
    if (!result)
    {
        return Result::fail(result.code, Severity::Fatal,
                            "Failed to create render frame graph.");
    }

    if (m_drawResources != nullptr)
    {
        m_frameGraph->add_pass(
            std::make_unique<DrawSystem::RenderableInfoCopyPass>(
                m_drawFrameState,
                m_drawResources->renderable_info_buffer_handle()));
        m_frameGraph->add_pass(
            std::make_unique<DrawSystem::TransformBufferCopyPass>(
                m_drawFrameState, m_drawResources->transform_buffer_handle()));
        m_frameGraph->add_pass(
            std::make_unique<DrawSystem::ViewProjectionCopyPass>(
                m_drawResources->view_projection_buffer_handle()));
            m_frameGraph->add_pass(
                std::make_unique<DrawSystem::MaterialBufferCopyPass>(
                    m_drawResources->material_buffer_handle()));
            if (m_lightResources != nullptr)
            {
                m_frameGraph->add_pass(
                    std::make_unique<LightingSystem::LightBufferCopyPass>(
                        "LightFrameBufferCopy",
                        m_lightResources->frame_buffer_handle(),
                        sizeof(GpuData::LightFrameGpu)));
                m_frameGraph->add_pass(
                    std::make_unique<LightingSystem::LightBufferCopyPass>(
                        "PointLightBufferCopy",
                        m_lightResources->point_light_buffer_handle(),
                        static_cast<uint64_t>(m_pointLightBufferCapacity) *
                            sizeof(GpuData::PointLightGpu)));
            }
            m_frameGraph->add_pass(
                std::make_unique<DrawSystem::InitializeHiZDepthPass>());
            m_frameGraph->add_pass(
                std::make_unique<DrawSystem::GenerateVisibleListPass>(
                    m_drawFrameState,
                    m_drawResources->renderable_info_buffer_handle(),
                    m_drawResources->view_projection_buffer_handle(),
                    m_drawResources->render_object_buffer_handle(),
                    m_drawResources->visible_object_count_buffer_handle(),
                    m_drawResources->visible_object_count_buffer_uav_handle()));
        m_frameGraph->add_pass(
                std::make_unique<DrawSystem::StaticMeshBatchingPass>(
                    m_drawFrameState,
                    m_drawResources->render_object_buffer_handle(),
                    m_drawResources->transform_buffer_handle(),
                    m_drawResources->visible_object_count_buffer_handle(),
                    m_maxObjectCount));
        m_frameGraph->add_pass(
            std::make_unique<DrawSystem::StaticMeshForwardPass>(
                m_drawFrameState,
                m_drawResources->render_object_buffer_handle(),
                    m_drawResources->transform_buffer_handle(),
                    m_drawResources->view_projection_buffer_handle(),
                    m_drawResources->visible_object_count_buffer_handle(),
                    m_drawResources->material_buffer_handle(),
                    m_lightResources->frame_buffer_handle(),
                    m_lightResources->point_light_buffer_handle(),
                    m_maxObjectCount));
        m_frameGraph->add_pass(
            std::make_unique<DrawSystem::BuildHiZDepthPass>());
        }

    result = m_frameGraph->build();
    if (!result)
    {
        return result;
    }

    RHI::FrameGraphDesc presentFrameGraphDesc{};
    presentFrameGraphDesc.usePresentQueue = true;
    presentFrameGraphDesc.enableProfiling = true;
    presentFrameGraphDesc.waitForCompletion = true;
    result = m_renderBackend->create_frame_graph(presentFrameGraphDesc,
                                                 m_presentFrameGraph);
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
} // namespace Cue
