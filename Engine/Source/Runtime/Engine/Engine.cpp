#include "Engine.h"
#include "GpuData/Batching.h"
#include "GpuData/Transform.h"
#include "Passes/TransformBufferCopyPass.h"
#include "Passes/UploadBufferCopyPass.h"
#include "Passes/GenerateVisibleList.h"
#include "Passes/StaticMeshBatchingPass.h"
#include "Passes/StaticMeshForwardPass.h"
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

        auto* staticMeshPool = m_backend->get_static_mesh_pool();
    if (staticMeshPool == nullptr)
    {
        return Result::fail(
            Code::NotFound,
            Severity::Fatal,
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
        return Result::fail(
            Code::NotFound,
            Severity::Fatal,
            "Cube model does not contain any mesh data.");
    }
    const uint32_t cubeIndexCount = static_cast<uint32_t>(cubeModelData.meshes[0].indices.size());

    RHI::staticMeshHandle cubeStaticMeshHandle{};
    result = m_assetManager.get_static_mesh_handle(cubeModelHandle, 0, cubeStaticMeshHandle);
    if (!result)
    {
        return result;
    }

    uint32_t cubeMeshId = 0;
    result = staticMeshPool->get_mesh_id(cubeStaticMeshHandle, cubeMeshId);
    if (!result)
    {
        return result;
    }

    // GenerateVisibleObjectList 用バッファを作成
        auto* bufferManager = m_backend->get_buffer_manager();
    if (bufferManager == nullptr)
    {
        return Result::fail(
            Code::NotFound,
            Severity::Fatal,
            "Failed to get buffer manager from backend.");
    }

    constexpr uint32_t k_maxObjectCount = 1000; // TODO: 実際のオブジェクト数管理へ置き換える
    constexpr uint32_t k_initialObjectCount = 3;

    RHI::BufferDesc objectInfoBufferDesc{};
    objectInfoBufferDesc.name = "ObjectInfoBuffer";
    objectInfoBufferDesc.type = RHI::BufferType::Structured;
    objectInfoBufferDesc.defaultHeapCount = 1;
    objectInfoBufferDesc.uploadHeapCount = 1;
    objectInfoBufferDesc.initialState = RHI::ResourceState::ShaderResource;
    objectInfoBufferDesc.stride = sizeof(GpuData::ObjectInfo);
    objectInfoBufferDesc.elementCount = k_maxObjectCount;
    objectInfoBufferDesc.size = objectInfoBufferDesc.stride * objectInfoBufferDesc.elementCount;
    objectInfoBufferDesc.alignment = alignof(GpuData::ObjectInfo);
    RHI::bufferHandle objectInfoBufferHandle{};
    result = bufferManager->create_buffer(objectInfoBufferDesc, objectInfoBufferHandle);
    if (!result)
    {
        return result;
    }

    std::vector<RHI::SlotUploader<GpuData::ObjectInfo>> objectInfoUploaders{};
    result = bufferManager->create_slot_uploaders(objectInfoBufferHandle, 1, objectInfoUploaders);
    if (!result)
    {
        return result;
    }
    if (objectInfoUploaders.size() != 1)
    {
        return Result::fail(
            Code::InternalError,
            Severity::Fatal,
            "ObjectInfoBuffer uploader was not created.");
    }

    objectInfoUploaders[0].begin_frame();
    if (!objectInfoUploaders[0].push(0, GpuData::ObjectInfo{.objectId = 0, .visible = 1, .meshId = cubeMeshId, .transformId = 0}) ||
        !objectInfoUploaders[0].push(1, GpuData::ObjectInfo{.objectId = 1, .visible = 0, .meshId = cubeMeshId, .transformId = 1}) ||
        !objectInfoUploaders[0].push(2, GpuData::ObjectInfo{.objectId = 2, .visible = 1, .meshId = cubeMeshId, .transformId = 2}) ||
        !objectInfoUploaders[0].commit())
    {
        return Result::fail(
            Code::InternalError,
            Severity::Fatal,
            "Failed to write initial object infos by SlotUploader.");
    }

    RHI::BufferDesc transformBufferDesc{};
    transformBufferDesc.name = "TransformBuffer";
    transformBufferDesc.type = RHI::BufferType::Structured;
    transformBufferDesc.defaultHeapCount = 1;
    transformBufferDesc.uploadHeapCount = 1;
    transformBufferDesc.initialState = RHI::ResourceState::ShaderResource;
    transformBufferDesc.stride = sizeof(GpuData::ObjectTransformGpu);
    transformBufferDesc.elementCount = k_maxObjectCount;
    transformBufferDesc.size = transformBufferDesc.stride * transformBufferDesc.elementCount;
    transformBufferDesc.alignment = alignof(GpuData::ObjectTransformGpu);
    RHI::bufferHandle transformBufferHandle{};
    result = bufferManager->create_buffer(transformBufferDesc, transformBufferHandle);
    if (!result)
    {
        return result;
    }

    result = bufferManager->create_slot_uploaders(transformBufferHandle, 1, m_transformUploaders);
    if (!result)
    {
        return result;
    }
    if (m_transformUploaders.size() != 1)
    {
        return Result::fail(
            Code::InternalError,
            Severity::Fatal,
            "TransformBuffer uploader was not created.");
    }

    m_transformBufferHandle = transformBufferHandle;
    m_localTransforms =
        {
            GpuData::LocalTransform{},
            GpuData::LocalTransform{.position = Math::float3{2.0f, 0.0f, 0.0f}},
            GpuData::LocalTransform{.position = Math::float3{-2.0f, 0.0f, 0.0f}}};
    update_transform_buffer(0);

    RHI::BufferDesc renderObjectBufferDesc{};
    renderObjectBufferDesc.name = "RenderObjectBuffer";
    renderObjectBufferDesc.type = RHI::BufferType::UnorderedAccess;
    renderObjectBufferDesc.defaultHeapCount = 1;
    renderObjectBufferDesc.uploadHeapCount = 0;
    renderObjectBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
    renderObjectBufferDesc.stride = sizeof(GpuData::RenderObject);
    renderObjectBufferDesc.elementCount = k_maxObjectCount;
    renderObjectBufferDesc.size = renderObjectBufferDesc.stride * renderObjectBufferDesc.elementCount;
    renderObjectBufferDesc.alignment = alignof(GpuData::RenderObject);
    RHI::bufferHandle renderObjectBufferHandle{};
    result = bufferManager->create_buffer(renderObjectBufferDesc, renderObjectBufferHandle);
    if (!result)
    {
        return result;
    }

    RHI::BufferDesc renderObjectCountBufferDesc{};
    renderObjectCountBufferDesc.name = "VisibleObjectCountBuffer";
    renderObjectCountBufferDesc.type = RHI::BufferType::Raw;
    renderObjectCountBufferDesc.defaultHeapCount = 1;
    renderObjectCountBufferDesc.uploadHeapCount = 1;
    renderObjectCountBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
    renderObjectCountBufferDesc.stride = sizeof(uint32_t);
    renderObjectCountBufferDesc.elementCount = 1;
    renderObjectCountBufferDesc.size = sizeof(uint32_t);
    renderObjectCountBufferDesc.alignment = alignof(uint32_t);
    RHI::bufferHandle renderObjectCountBufferHandle{};
    result = bufferManager->create_buffer(renderObjectCountBufferDesc, renderObjectCountBufferHandle);
    if (!result)
    {
        return result;
    }

    RHI::BufferDesc indirectCommandBufferDesc{};
    indirectCommandBufferDesc.name = "IndirectCommandBuffer";
    indirectCommandBufferDesc.type = RHI::BufferType::UnorderedAccess;
    indirectCommandBufferDesc.defaultHeapCount = 1;
    indirectCommandBufferDesc.uploadHeapCount = 0;
    indirectCommandBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
    indirectCommandBufferDesc.stride = sizeof(GpuData::IndirectCommand);
    indirectCommandBufferDesc.elementCount = k_maxObjectCount;
    indirectCommandBufferDesc.size = indirectCommandBufferDesc.stride * indirectCommandBufferDesc.elementCount;
    indirectCommandBufferDesc.alignment = alignof(GpuData::IndirectCommand);
    RHI::bufferHandle indirectCommandBufferHandle{};
    result = bufferManager->create_buffer(indirectCommandBufferDesc, indirectCommandBufferHandle);
    if (!result)
    {
        return result;
    }

    RHI::BufferDesc indirectCommandCountBufferDesc{};
    indirectCommandCountBufferDesc.name = "IndirectCommandCountBuffer";
    indirectCommandCountBufferDesc.type = RHI::BufferType::Raw;
    indirectCommandCountBufferDesc.defaultHeapCount = 1;
    indirectCommandCountBufferDesc.uploadHeapCount = 0;
    indirectCommandCountBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
    indirectCommandCountBufferDesc.stride = sizeof(uint32_t);
    indirectCommandCountBufferDesc.elementCount = 1;
    indirectCommandCountBufferDesc.size = sizeof(uint32_t);
    indirectCommandCountBufferDesc.alignment = alignof(uint32_t);
    RHI::bufferHandle indirectCommandCountBufferHandle{};
    result = bufferManager->create_buffer(indirectCommandCountBufferDesc, indirectCommandCountBufferHandle);
    if (!result)
    {
        return result;
    }

        auto* viewManager = m_backend->get_view_manager();
    if (viewManager == nullptr)
    {
        return Result::fail(
            Code::NotFound,
            Severity::Fatal,
            "Failed to get view manager from backend.");
    }

    RHI::ViewDesc objectInfoBufferSrvDesc{};
    objectInfoBufferSrvDesc.name = "ObjectInfoBufferSRV";
    objectInfoBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
    objectInfoBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
    objectInfoBufferSrvDesc.bufferHandle = objectInfoBufferHandle;
    objectInfoBufferSrvDesc.firstElement = 0;
    objectInfoBufferSrvDesc.numElements = objectInfoBufferDesc.elementCount;
    objectInfoBufferSrvDesc.structureByteStride = objectInfoBufferDesc.stride;
    RHI::viewHandle objectInfoBufferSrvHandle{};
    result = viewManager->create_view(objectInfoBufferSrvDesc, objectInfoBufferSrvHandle);
    if (!result)
    {
        return result;
    }

    RHI::ViewDesc transformBufferSrvDesc{};
    transformBufferSrvDesc.name = "TransformBufferSRV";
    transformBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
    transformBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
    transformBufferSrvDesc.bufferHandle = transformBufferHandle;
    transformBufferSrvDesc.firstElement = 0;
    transformBufferSrvDesc.numElements = transformBufferDesc.elementCount;
    transformBufferSrvDesc.structureByteStride = transformBufferDesc.stride;
    RHI::viewHandle transformBufferSrvHandle{};
    result = viewManager->create_view(transformBufferSrvDesc, transformBufferSrvHandle);
    if (!result)
    {
        return result;
    }

    RHI::ViewDesc renderObjectBufferUavDesc{};
    renderObjectBufferUavDesc.name = "RenderObjectBufferUAV";
    renderObjectBufferUavDesc.type = RHI::ViewType::UnorderedAccessBuffer;
    renderObjectBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
    renderObjectBufferUavDesc.bufferHandle = renderObjectBufferHandle;
    renderObjectBufferUavDesc.firstElement = 0;
    renderObjectBufferUavDesc.numElements = renderObjectBufferDesc.elementCount;
    renderObjectBufferUavDesc.structureByteStride = renderObjectBufferDesc.stride;
    RHI::viewHandle renderObjectBufferUavHandle{};
    result = viewManager->create_view(renderObjectBufferUavDesc, renderObjectBufferUavHandle);
    if (!result)
    {
        return result;
    }

    RHI::ViewDesc indirectCommandBufferUavDesc{};
    indirectCommandBufferUavDesc.name = "IndirectCommandBufferUAV";
    indirectCommandBufferUavDesc.type = RHI::ViewType::UnorderedAccessBuffer;
    indirectCommandBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
    indirectCommandBufferUavDesc.bufferHandle = indirectCommandBufferHandle;
    indirectCommandBufferUavDesc.firstElement = 0;
    indirectCommandBufferUavDesc.numElements = indirectCommandBufferDesc.elementCount;
    indirectCommandBufferUavDesc.structureByteStride = indirectCommandBufferDesc.stride;
    RHI::viewHandle indirectCommandBufferUavHandle{};
    result = viewManager->create_view(indirectCommandBufferUavDesc, indirectCommandBufferUavHandle);
    if (!result)
    {
        return result;
    }

    RHI::ViewDesc indirectCommandCountBufferUavDesc{};
    indirectCommandCountBufferUavDesc.name = "IndirectCommandCountBufferUAV";
    indirectCommandCountBufferUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    indirectCommandCountBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
    indirectCommandCountBufferUavDesc.bufferHandle = indirectCommandCountBufferHandle;
    indirectCommandCountBufferUavDesc.firstElement = 0;
    indirectCommandCountBufferUavDesc.numElements = indirectCommandCountBufferDesc.size / sizeof(uint32_t);
    RHI::viewHandle indirectCommandCountBufferUavHandle{};
    result = viewManager->create_view(indirectCommandCountBufferUavDesc, indirectCommandCountBufferUavHandle);
    if (!result)
    {
        return result;
    }

    RHI::ViewDesc renderObjectCountBufferUavDesc{};
    renderObjectCountBufferUavDesc.name = "VisibleObjectCountBufferUAV";
    renderObjectCountBufferUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    renderObjectCountBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
    renderObjectCountBufferUavDesc.bufferHandle = renderObjectCountBufferHandle;
    renderObjectCountBufferUavDesc.firstElement = 0;
    renderObjectCountBufferUavDesc.numElements = renderObjectCountBufferDesc.size / sizeof(uint32_t);
    RHI::viewHandle renderObjectCountBufferUavHandle{};
    result = viewManager->create_view(renderObjectCountBufferUavDesc, renderObjectCountBufferUavHandle);
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
    finalColorDesc.clearColor[0] = 0.0f;
    finalColorDesc.clearColor[1] = 0.5f;
    finalColorDesc.clearColor[2] = 0.0f;
    finalColorDesc.clearColor[3] = 1.0f;
    RHI::textureHandle finalColorHandle{};
    auto textureManager = m_backend->get_texture_manager();
    textureManager->create_texture(finalColorDesc, finalColorHandle);
    RHI::ViewDesc finalColorRtvDesc{};
    finalColorRtvDesc.name = "FinalColorRTV";
    finalColorRtvDesc.type = RHI::ViewType::RenderTarget;
    finalColorRtvDesc.bufferKind = RHI::BufferKind::Texture;
    finalColorRtvDesc.textureHandle = finalColorHandle;
    finalColorRtvDesc.colorFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
    RHI::viewHandle finalColorRtvHandle{};
    viewManager->create_view(finalColorRtvDesc, finalColorRtvHandle);
    RHI::ViewDesc finalColorSrvDesc{};
    finalColorSrvDesc.name = "FinalColorSRV";
    finalColorSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
    finalColorSrvDesc.bufferKind = RHI::BufferKind::Texture;
    finalColorSrvDesc.textureHandle = finalColorHandle;
    finalColorSrvDesc.colorFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
    finalColorSrvDesc.mipLevels = 1;
    RHI::viewHandle finalColorSrvHandle{};
    viewManager->create_view(finalColorSrvDesc, finalColorSrvHandle);

    // render 用 FrameGraph の生成
    RHI::FrameGraphDesc frameGraphDesc{};
    frameGraphDesc.usePresentQueue = false;
    result = m_backend->create_frame_graph(frameGraphDesc, m_frameGraph);
    if (!result)
    {
        return Result::fail(
            result.code,
            Severity::Fatal,
            "Failed to create render frame graph.");
    }

    m_frameGraph->add_pass(std::make_unique<UploadBufferCopyPass>(
        "ObjectInfoBuffer",
        static_cast<uint64_t>(k_initialObjectCount) * sizeof(GpuData::ObjectInfo)));
    m_frameGraph->add_pass(std::make_unique<TransformBufferCopyPass>(
        static_cast<uint64_t>(k_initialObjectCount) * sizeof(GpuData::ObjectTransformGpu)));
    m_frameGraph->add_pass(std::make_unique<GenerateVisibleListPass>(k_initialObjectCount));
    m_frameGraph->add_pass(std::make_unique<StaticMeshBatchingPass>(k_initialObjectCount));
    m_frameGraph->add_pass(std::make_unique<StaticMeshForwardPass>(k_initialObjectCount, cubeIndexCount));
    result = m_frameGraph->build();
    if (!result)
    {
        return Result::fail(
            result.code,
            Severity::Fatal,
            "Failed to build render frame graph.");
    }

    // present 用 FrameGraph の生成
    RHI::FrameGraphDesc presentFrameGraphDesc{};
    presentFrameGraphDesc.usePresentQueue = true;
    result = m_backend->create_frame_graph(presentFrameGraphDesc, m_presentFrameGraph);
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
        return result;
    }

    // GameCore の生成
    m_gameCore = std::make_unique<GameCore>();
    m_gameCore->initialize();

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
        (void)a_frameNo;

        const float deltaTime = (m_frameController != nullptr)
                                    ? static_cast<float>(m_frameController->frame_counter().delta_time())
                                    : 0.0f;

        if (m_localTransforms.size() >= 3)
        {
            m_localTransforms[0].rotation.m_y += deltaTime * 1.25f;
            m_localTransforms[1].rotation.m_x += deltaTime * 0.75f;
            m_localTransforms[2].rotation.m_y -= deltaTime * 1.0f;
        }

        update_transform_buffer(a_index);
    };
}

std::function<void(uint64_t, uint32_t)> Engine::render()
{
    return [this](uint64_t a_frameNo, uint32_t a_index)
    {
        Result r = m_backend->render(a_frameNo, a_index, *m_frameGraph);
        if (!r)
        {
            CUE_ASSERTF(false, "Render failed: %s (code: %u, severity: %u)", r.message.data(), static_cast<uint32_t>(r.code), static_cast<uint32_t>(r.severity));
        }
    };
}

std::function<void(uint64_t, uint32_t)> Engine::present()
{
    return [this](uint64_t a_frameNo, uint32_t a_index)
    {
        m_backend->present(a_frameNo, a_index, false, *m_presentFrameGraph);
    };
}

void Engine::update_transform_buffer(uint32_t a_bufferIndex)
{
    if (!m_transformBufferHandle.valid() || m_transformUploaders.empty())
    {
        return;
    }

    const uint32_t uploaderIndex = (m_transformUploaders.size() == 1)
                                       ? 0u
                                       : a_bufferIndex;
    if (uploaderIndex >= m_transformUploaders.size())
    {
        return;
    }

    auto &uploader = m_transformUploaders[uploaderIndex];
    uploader.begin_frame();

    for (size_t objectIndex = 0; objectIndex < m_localTransforms.size(); ++objectIndex)
    {
        const GpuData::LocalTransform &localTransform = m_localTransforms[objectIndex];
        const GpuData::ObjectTransformGpu transformGpu{
            .worldMatrix = Math::make_affine_matrix(
                localTransform.scale,
                localTransform.rotation,
                localTransform.position)};

        if (!uploader.push(static_cast<uint32_t>(objectIndex), transformGpu))
        {
            CUE_ASSERTF(false, "Failed to queue transform upload. objectIndex=%zu", objectIndex);
            return;
        }
    }

    if (!uploader.commit())
    {
        CUE_ASSERTF(false, "Failed to commit transform uploads.");
    }
}
} // namespace Cue
