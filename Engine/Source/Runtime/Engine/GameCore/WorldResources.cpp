#include "WorldResources.h"
#include <GpuData/Batching.h>
#include <GpuData/Transform.h>
#include <GpuData/ViewProjection.h>

namespace Cue
{
    Result WorldResources::create_renderable_info_buffer(
        const uint32_t a_maxObjectCount)
    {
        // RenderableInfoBuffer の設定
        RHI::BufferDesc renderableInfoBufferDesc{};
        renderableInfoBufferDesc.name = "RenderableInfoBuffer";
        renderableInfoBufferDesc.type = RHI::BufferType::Structured;
        renderableInfoBufferDesc.defaultHeapCount = 1;
        renderableInfoBufferDesc.uploadHeapCount = 1;
        renderableInfoBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        renderableInfoBufferDesc.stride = sizeof(GpuData::RenderableInfo);
        renderableInfoBufferDesc.elementCount = a_maxObjectCount;
        renderableInfoBufferDesc.size =
            renderableInfoBufferDesc.stride *
            renderableInfoBufferDesc.elementCount;
        renderableInfoBufferDesc.alignment = alignof(GpuData::RenderableInfo);

        // RenderableInfoBuffer の作成
        RHI::BufferHandle& renderableInfoBufferHandle =
            m_bufferHandles[static_cast<size_t>(WorldResourceType::RenderableInfoBuffer)];
        Result result = m_bufferManager->create_buffer(
            renderableInfoBufferDesc, renderableInfoBufferHandle);
        if (!result)
        {
            return result;
        }

        // RenderableInfoBuffer の uploader 作成
        result = m_bufferManager->create_slot_uploaders(
            m_bufferHandles[static_cast<size_t>(WorldResourceType::RenderableInfoBuffer)],
            1, m_renderableInfoUploaders);
        if (!result)
        {
            return result;
        }
        if (m_renderableInfoUploaders.size() != 1)
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
            m_viewHandles[static_cast<size_t>(WorldResourceType::RenderableInfoBuffer)];
        result = m_viewManager->create_view(renderableInfoBufferSrvDesc,
            renderableInfoBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result WorldResources::create_transform_buffer(const uint32_t a_maxObjectCount)
    {
        // TransformBuffer の設定
        RHI::BufferDesc transformBufferDesc{};
        transformBufferDesc.name = "TransformBuffer";
        transformBufferDesc.type = RHI::BufferType::Structured;
        transformBufferDesc.defaultHeapCount = 1;
        transformBufferDesc.uploadHeapCount = 1;
        transformBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        transformBufferDesc.stride = sizeof(GpuData::ObjectTransformGpu);
        transformBufferDesc.elementCount = a_maxObjectCount;
        transformBufferDesc.size =
            transformBufferDesc.stride * transformBufferDesc.elementCount;
        transformBufferDesc.alignment = alignof(GpuData::ObjectTransformGpu);
        RHI::BufferHandle& transformBufferHandle =
            m_bufferHandles[static_cast<size_t>(WorldResourceType::TransformBuffer)];

        // TransformBuffer の作成
        Result result = m_bufferManager->create_buffer(transformBufferDesc, transformBufferHandle);
        if (!result)
        {
            return result;
        }

        // TransformBuffer の uploader 作成
        result = m_bufferManager->create_slot_uploaders(
            transformBufferHandle, 1,m_transformUploaders);
        if (!result)
        {
            return result;
        }
        if (m_transformUploaders.size() != 1)
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
            m_viewHandles[static_cast<size_t>(WorldResourceType::TransformBuffer)];
        result = m_viewManager->create_view(transformBufferSrvDesc,
            transformBufferSrvHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result WorldResources::create_view_projection_buffer()
    {
        constexpr uint32_t k_constantBufferAlignment = 256;

        RHI::BufferDesc viewProjectionBufferDesc{};
        viewProjectionBufferDesc.name = "ViewProjectionBuffer";
        viewProjectionBufferDesc.type = RHI::BufferType::Constant;
        viewProjectionBufferDesc.defaultHeapCount = 1;
        viewProjectionBufferDesc.uploadHeapCount = 1;
        viewProjectionBufferDesc.initialState = RHI::ResourceState::Common;
        viewProjectionBufferDesc.stride = sizeof(GpuData::ViewProjectionGpu);
        viewProjectionBufferDesc.elementCount = 1;
        viewProjectionBufferDesc.size =
            viewProjectionBufferDesc.stride * viewProjectionBufferDesc.elementCount;
        viewProjectionBufferDesc.alignment = k_constantBufferAlignment;

        RHI::BufferHandle& viewProjectionBufferHandle =
            m_bufferHandles[static_cast<size_t>(WorldResourceType::ViewProjectionBuffer)];
        Result result = m_bufferManager->create_buffer(
            viewProjectionBufferDesc, viewProjectionBufferHandle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            viewProjectionBufferHandle, 1, m_viewProjectionUploaders);
        if (!result)
        {
            return result;
        }
        if (m_viewProjectionUploaders.size() != 1)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "ViewProjectionBuffer uploader was not created.");
        }

        return Result::ok();
    }

    Result WorldResources::create_render_object_buffer(const uint32_t a_maxObjectCount)
    {
        // RenderObjectBuffer の設定
        RHI::BufferDesc renderObjectBufferDesc{};
        renderObjectBufferDesc.name = "RenderObjectBuffer";
        renderObjectBufferDesc.type = RHI::BufferType::UnorderedAccess;
        renderObjectBufferDesc.defaultHeapCount = 1;
        renderObjectBufferDesc.uploadHeapCount = 0;
        renderObjectBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
        renderObjectBufferDesc.stride = sizeof(GpuData::RenderObject);
        renderObjectBufferDesc.elementCount = a_maxObjectCount;
        renderObjectBufferDesc.size =
            renderObjectBufferDesc.stride * renderObjectBufferDesc.elementCount;
        renderObjectBufferDesc.alignment = alignof(GpuData::RenderObject);

        // RenderObjectBuffer の作成
        RHI::BufferHandle& renderObjectBufferHandle =
            m_bufferHandles[static_cast<size_t>(WorldResourceType::RenderObjectBuffer)];
        Result result = m_bufferManager->create_buffer(renderObjectBufferDesc,
            renderObjectBufferHandle);
        if (!result)
        {
            return result;
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
            m_viewHandles[static_cast<size_t>(WorldResourceType::RenderObjectBuffer)];
        result = m_viewManager->create_view(renderObjectBufferUavDesc,
            renderObjectBufferUavHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result WorldResources::create_object_count_buffer()
    {
        // ObjectCountBuffer の設定
        RHI::BufferDesc renderObjectCountBufferDesc{};
        renderObjectCountBufferDesc.name = "VisibleObjectCountBuffer";
        renderObjectCountBufferDesc.type = RHI::BufferType::Raw;
        renderObjectCountBufferDesc.defaultHeapCount = 1;
        renderObjectCountBufferDesc.uploadHeapCount = 1;
        renderObjectCountBufferDesc.initialState =
            RHI::ResourceState::UnorderedAccess;
        renderObjectCountBufferDesc.stride = sizeof(uint32_t);
        renderObjectCountBufferDesc.elementCount = 1;
        renderObjectCountBufferDesc.size = sizeof(uint32_t);
        renderObjectCountBufferDesc.alignment = alignof(uint32_t);

        // ObjectCountBuffer の作成
        RHI::BufferHandle& renderObjectCountBufferHandle =
            m_bufferHandles[static_cast<size_t>(WorldResourceType::VisibleObjectCountBuffer)];
        Result result = m_bufferManager->create_buffer(renderObjectCountBufferDesc,
            renderObjectCountBufferHandle);
        if (!result)
        {
            return result;
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
            m_viewHandles[static_cast<size_t>(WorldResourceType::VisibleObjectCountBuffer)];
        result = m_viewManager->create_view(renderObjectCountBufferUavDesc,
            renderObjectCountBufferUavHandle);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }
}
