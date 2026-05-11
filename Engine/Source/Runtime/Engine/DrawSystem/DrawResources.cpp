#include <DrawSystem/DrawResources.h>
#include <GpuData/Batching.h>
#include <GpuData/Sprite.h>
#include <GpuData/Transform.h>
#include <GpuData/ViewProjection.h>

namespace Cue::DrawSystem
{
    Result DrawResources::create_renderable_info_buffer(
        const uint32_t a_maxObjectCount)
    {
        // RenderableInfoBuffer の設定
        RHI::BufferDesc renderableInfoBufferDesc{};
        renderableInfoBufferDesc.name = "RenderableInfoBuffer";
        renderableInfoBufferDesc.type = RHI::BufferType::Structured;
        renderableInfoBufferDesc.defaultHeapCount = 1;
        renderableInfoBufferDesc.uploadHeapCount = m_bufferCount;
        renderableInfoBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        renderableInfoBufferDesc.stride = sizeof(GpuData::RenderableInfo);
        renderableInfoBufferDesc.elementCount = a_maxObjectCount;
        renderableInfoBufferDesc.size =
            renderableInfoBufferDesc.stride *
            renderableInfoBufferDesc.elementCount;
        renderableInfoBufferDesc.alignment = alignof(GpuData::RenderableInfo);

        // RenderableInfoBuffer の作成
        RHI::BufferHandle& renderableInfoBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderableInfoBuffer)];
        Result result = m_bufferManager->create_buffer(
            renderableInfoBufferDesc, renderableInfoBufferHandle);
        if (!result)
        {
            return result;
        }

        // RenderableInfoBuffer の uploader 作成
        result = m_bufferManager->create_slot_uploaders(
            m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderableInfoBuffer)],
            m_bufferCount, m_renderableInfoUploaders);
        if (!result)
        {
            return result;
        }
        if (m_renderableInfoUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "RenderableInfoBuffer uploader was not created.");
        }

        // RenderableInfoBuffer の SRV 作成
        RHI::ViewDesc renderableInfoBufferSrvDesc{};
        renderableInfoBufferSrvDesc.name = "RenderableInfoBufferSRV";
        renderableInfoBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        renderableInfoBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        renderableInfoBufferSrvDesc.bufferHandle = renderableInfoBufferHandle;
        renderableInfoBufferSrvDesc.firstElement = 0;
        renderableInfoBufferSrvDesc.numElements =
            renderableInfoBufferDesc.elementCount;
        renderableInfoBufferSrvDesc.structureByteStride =
            renderableInfoBufferDesc.stride;
        
        // ビューの作成
        RHI::ViewHandle& renderableInfoBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::RenderableInfoBuffer)];
        result = m_viewManager->create_view(renderableInfoBufferSrvDesc,
            renderableInfoBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_transform_buffer(const uint32_t a_maxObjectCount)
    {
        // TransformBuffer の設定
        RHI::BufferDesc transformBufferDesc{};
        transformBufferDesc.name = "TransformBuffer";
        transformBufferDesc.type = RHI::BufferType::Structured;
        transformBufferDesc.defaultHeapCount = 1;
        transformBufferDesc.uploadHeapCount = m_bufferCount;
        transformBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        transformBufferDesc.stride = sizeof(GpuData::ObjectTransformGpu);
        transformBufferDesc.elementCount = a_maxObjectCount;
        transformBufferDesc.size =
            transformBufferDesc.stride * transformBufferDesc.elementCount;
        transformBufferDesc.alignment = alignof(GpuData::ObjectTransformGpu);
        RHI::BufferHandle& transformBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::TransformBuffer)];

        // TransformBuffer の作成
        Result result = m_bufferManager->create_buffer(transformBufferDesc, transformBufferHandle);
        if (!result)
        {
            return result;
        }

        // TransformBuffer の uploader 作成
        result = m_bufferManager->create_slot_uploaders(
            transformBufferHandle, m_bufferCount, m_transformUploaders);
        if (!result)
        {
            return result;
        }
        if (m_transformUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal,
                "TransformBuffer uploader was not created.");
        }

        // TransformBuffer の SRV 作成
        RHI::ViewDesc transformBufferSrvDesc{};
        transformBufferSrvDesc.name = "TransformBufferSRV";
        transformBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        transformBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        transformBufferSrvDesc.bufferHandle = transformBufferHandle;
        transformBufferSrvDesc.firstElement = 0;
        transformBufferSrvDesc.numElements = transformBufferDesc.elementCount;
        transformBufferSrvDesc.structureByteStride = transformBufferDesc.stride;

        // ビューの作成
        RHI::ViewHandle& transformBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::TransformBuffer)];
        result = m_viewManager->create_view(transformBufferSrvDesc,
            transformBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_view_projection_buffer()
    {
        constexpr uint32_t k_constantBufferAlignment = 256;

        RHI::BufferDesc viewProjectionBufferDesc{};
        viewProjectionBufferDesc.name = "ViewProjectionBuffer";
        viewProjectionBufferDesc.type = RHI::BufferType::Constant;
        viewProjectionBufferDesc.defaultHeapCount = 1;
        viewProjectionBufferDesc.uploadHeapCount = m_bufferCount;
        viewProjectionBufferDesc.initialState = RHI::ResourceState::Common;
        viewProjectionBufferDesc.stride = sizeof(GpuData::ViewProjectionGpu);
        viewProjectionBufferDesc.elementCount = 1;
        viewProjectionBufferDesc.size =
            viewProjectionBufferDesc.stride * viewProjectionBufferDesc.elementCount;
        viewProjectionBufferDesc.alignment = k_constantBufferAlignment;

        RHI::BufferHandle& viewProjectionBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::ViewProjectionBuffer)];
        Result result = m_bufferManager->create_buffer(
            viewProjectionBufferDesc, viewProjectionBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            viewProjectionBufferHandle, m_bufferCount, m_viewProjectionUploaders);
        if (!result)
        {
            return result;
        }
        if (m_viewProjectionUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "ViewProjectionBuffer uploader was not created.");
        }

        return Result::ok();
    }

    Result DrawResources::create_material_buffer(const uint32_t a_maxMaterialCount)
    {
        RHI::BufferDesc materialBufferDesc{};
        materialBufferDesc.name = "MaterialBuffer";
        materialBufferDesc.type = RHI::BufferType::Structured;
        materialBufferDesc.defaultHeapCount = 1;
        materialBufferDesc.uploadHeapCount = m_bufferCount;
        materialBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        materialBufferDesc.stride = sizeof(GpuData::MaterialGpu);
        materialBufferDesc.elementCount = a_maxMaterialCount;
        materialBufferDesc.size =
            materialBufferDesc.stride * materialBufferDesc.elementCount;
        materialBufferDesc.alignment = alignof(GpuData::MaterialGpu);

        RHI::BufferHandle& materialBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::MaterialBuffer)];
        Result result = m_bufferManager->create_buffer(
            materialBufferDesc, materialBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            materialBufferHandle, m_bufferCount, m_materialUploaders);
        if (!result)
        {
            return result;
        }
        if (m_materialUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "MaterialBuffer uploader was not created.");
        }

        RHI::ViewDesc materialBufferSrvDesc{};
        materialBufferSrvDesc.name = "MaterialBufferSRV";
        materialBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        materialBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        materialBufferSrvDesc.bufferHandle = materialBufferHandle;
        materialBufferSrvDesc.firstElement = 0;
        materialBufferSrvDesc.numElements = materialBufferDesc.elementCount;
        materialBufferSrvDesc.structureByteStride = materialBufferDesc.stride;

        RHI::ViewHandle& materialBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::MaterialBuffer)];
        result = m_viewManager->create_view(
            materialBufferSrvDesc, materialBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_render_object_buffer(const uint32_t a_maxObjectCount)
    {
        // RenderObjectBuffer の設定
        RHI::BufferDesc renderObjectBufferDesc{};
        renderObjectBufferDesc.name = "RenderObjectBuffer";
        renderObjectBufferDesc.type = RHI::BufferType::UnorderedAccess;
        renderObjectBufferDesc.defaultHeapCount = 1;
        renderObjectBufferDesc.uploadHeapCount = m_bufferCount;
        renderObjectBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
        renderObjectBufferDesc.stride = sizeof(GpuData::RenderObject);
        renderObjectBufferDesc.elementCount = a_maxObjectCount;
        renderObjectBufferDesc.size =
            renderObjectBufferDesc.stride * renderObjectBufferDesc.elementCount;
        renderObjectBufferDesc.alignment = alignof(GpuData::RenderObject);

        // RenderObjectBuffer の作成
        RHI::BufferHandle& renderObjectBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::RenderObjectBuffer)];
        Result result = m_bufferManager->create_buffer(renderObjectBufferDesc,
            renderObjectBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            renderObjectBufferHandle, m_bufferCount, m_renderObjectUploaders);
        if (!result)
        {
            return result;
        }
        if (m_renderObjectUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "RenderObjectBuffer uploader was not created.");
        }

        // RenderObjectBuffer の UAV 作成
        RHI::ViewDesc renderObjectBufferUavDesc{};
        renderObjectBufferUavDesc.name = "RenderObjectBufferUAV";
        renderObjectBufferUavDesc.type = RHI::ViewType::UnorderedAccessBuffer;
        renderObjectBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
        renderObjectBufferUavDesc.bufferHandle = renderObjectBufferHandle;
        renderObjectBufferUavDesc.firstElement = 0;
        renderObjectBufferUavDesc.numElements = renderObjectBufferDesc.elementCount;
        renderObjectBufferUavDesc.structureByteStride = renderObjectBufferDesc.stride;

        // ビューの作成
        RHI::ViewHandle& renderObjectBufferUavHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::RenderObjectBuffer)];
        result = m_viewManager->create_view(renderObjectBufferUavDesc,
            renderObjectBufferUavHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_object_count_buffer()
    {
        // ObjectCountBuffer の設定
        RHI::BufferDesc renderObjectCountBufferDesc{};
        renderObjectCountBufferDesc.name = "VisibleObjectCountBuffer";
        renderObjectCountBufferDesc.type = RHI::BufferType::Raw;
        renderObjectCountBufferDesc.defaultHeapCount = 1;
        renderObjectCountBufferDesc.uploadHeapCount = m_bufferCount;
        renderObjectCountBufferDesc.initialState =
            RHI::ResourceState::UnorderedAccess;
        renderObjectCountBufferDesc.stride = sizeof(uint32_t);
        renderObjectCountBufferDesc.elementCount = 1;
        renderObjectCountBufferDesc.size = sizeof(uint32_t);
        renderObjectCountBufferDesc.alignment = alignof(uint32_t);

        // ObjectCountBuffer の作成
        RHI::BufferHandle& renderObjectCountBufferHandle =
            m_bufferHandles[static_cast<size_t>(DrawResourceType::VisibleObjectCountBuffer)];
        Result result = m_bufferManager->create_buffer(renderObjectCountBufferDesc,
            renderObjectCountBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            renderObjectCountBufferHandle, m_bufferCount, m_visibleObjectCountUploaders);
        if (!result)
        {
            return result;
        }
        if (m_visibleObjectCountUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "VisibleObjectCountBuffer uploader was not created.");
        }

        // ObjectCountBuffer の UAV 作成
        RHI::ViewDesc renderObjectCountBufferUavDesc{};
        renderObjectCountBufferUavDesc.name = "VisibleObjectCountBufferUAV";
        renderObjectCountBufferUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
        renderObjectCountBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
        renderObjectCountBufferUavDesc.bufferHandle = renderObjectCountBufferHandle;
        renderObjectCountBufferUavDesc.firstElement = 0;
        renderObjectCountBufferUavDesc.numElements =
            renderObjectCountBufferDesc.size / sizeof(uint32_t);

        // ビューの作成
        RHI::ViewHandle& renderObjectCountBufferUavHandle =
            m_viewHandles[static_cast<size_t>(DrawResourceType::VisibleObjectCountBuffer)];
        result = m_viewManager->create_view(renderObjectCountBufferUavDesc,
            renderObjectCountBufferUavHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result DrawResources::create_sprite_instance_buffer(
        const uint32_t a_maxSpriteCount)
    {
        RHI::BufferDesc spriteInstanceBufferDesc{};
        spriteInstanceBufferDesc.name = "SpriteInstanceBuffer";
        spriteInstanceBufferDesc.type = RHI::BufferType::Structured;
        spriteInstanceBufferDesc.defaultHeapCount = 1;
        spriteInstanceBufferDesc.uploadHeapCount = m_bufferCount;
        spriteInstanceBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        spriteInstanceBufferDesc.stride = sizeof(GpuData::SpriteInstanceGpu);
        spriteInstanceBufferDesc.elementCount = a_maxSpriteCount;
        spriteInstanceBufferDesc.size =
            spriteInstanceBufferDesc.stride *
            spriteInstanceBufferDesc.elementCount;
        spriteInstanceBufferDesc.alignment = alignof(GpuData::SpriteInstanceGpu);

        RHI::BufferHandle& spriteInstanceBufferHandle =
            m_bufferHandles[static_cast<size_t>(
                DrawResourceType::SpriteInstanceBuffer)];
        Result result = m_bufferManager->create_buffer(
            spriteInstanceBufferDesc, spriteInstanceBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            spriteInstanceBufferHandle, m_bufferCount, m_spriteInstanceUploaders);
        if (!result)
        {
            return result;
        }
        if (m_spriteInstanceUploaders.size() != m_bufferCount)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "SpriteInstanceBuffer uploader was not created.");
        }

        RHI::ViewDesc spriteInstanceBufferSrvDesc{};
        spriteInstanceBufferSrvDesc.name = "SpriteInstanceBufferSRV";
        spriteInstanceBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        spriteInstanceBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        spriteInstanceBufferSrvDesc.bufferHandle = spriteInstanceBufferHandle;
        spriteInstanceBufferSrvDesc.firstElement = 0;
        spriteInstanceBufferSrvDesc.numElements =
            spriteInstanceBufferDesc.elementCount;
        spriteInstanceBufferSrvDesc.structureByteStride =
            spriteInstanceBufferDesc.stride;

        RHI::ViewHandle& spriteInstanceBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(
                DrawResourceType::SpriteInstanceBuffer)];
        result = m_viewManager->create_view(
            spriteInstanceBufferSrvDesc, spriteInstanceBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }
}
