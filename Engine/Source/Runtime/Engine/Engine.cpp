#include "Engine.h"
#include "GpuData/Batching.h"
#include "passes/ObjectInfoCopyPass.h"
#include "passes/GenerateVisivleList.h"
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
        RHI::BufferHandle objectInfoBufferHandle{};
        Result result = bufferManager->create_buffer(objectInfoBufferDesc, objectInfoBufferHandle);
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
        if (!objectInfoUploaders[0].push(0, GpuData::ObjectInfo{ .objectId = 0, .visible = 1, .meshId = 0, .transformId = 0 }) ||
            !objectInfoUploaders[0].push(1, GpuData::ObjectInfo{ .objectId = 1, .visible = 0, .meshId = 0, .transformId = 1 }) ||
            !objectInfoUploaders[0].push(2, GpuData::ObjectInfo{ .objectId = 2, .visible = 1, .meshId = 0, .transformId = 2 }) ||
            !objectInfoUploaders[0].commit())
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "Failed to write initial object infos by SlotUploader.");
        }

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
        RHI::BufferHandle renderObjectBufferHandle{};
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
        RHI::BufferHandle renderObjectCountBufferHandle{};
        result = bufferManager->create_buffer(renderObjectCountBufferDesc, renderObjectCountBufferHandle);
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
        RHI::ViewHandle objectInfoBufferSrvHandle{};
        result = viewManager->create_view(objectInfoBufferSrvDesc, objectInfoBufferSrvHandle);
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
        RHI::ViewHandle renderObjectBufferUavHandle{};
        result = viewManager->create_view(renderObjectBufferUavDesc, renderObjectBufferUavHandle);
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
        RHI::ViewHandle renderObjectCountBufferUavHandle{};
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
        result = m_backend->create_frame_graph(m_frameGraph);
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Fatal,
                "Failed to create render frame graph.");
        }

        m_frameGraph->add_pass(std::make_unique<ObjectInfoCopyPass>(static_cast<uint64_t>(k_initialObjectCount) * sizeof(GpuData::ObjectInfo)));
        m_frameGraph->add_pass(std::make_unique<GenerateVisibleListPass>(k_initialObjectCount));
        result = m_frameGraph->build();
        if (!result)
        {
            return Result::fail(
                result.code,
                Severity::Fatal,
                "Failed to build render frame graph.");
        }

        // present 用 FrameGraph の生成
        result = m_backend->create_frame_graph(m_presentFrameGraph);
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
