#include "WorldResources.h"
#include <GpuData/Batching.h>
#include <GpuData/Transform.h>

namespace Cue
{
    Result WorldResources::create_object_info_buffer(const uint32_t a_maxObjectCount)
    {
        // ObjectInfoBuffer の設定
        RHI::BufferDesc objectInfoBufferDesc{};
        objectInfoBufferDesc.name = "ObjectInfoBuffer";
        objectInfoBufferDesc.type = RHI::BufferType::Structured;
        objectInfoBufferDesc.defaultHeapCount = 1;
        objectInfoBufferDesc.uploadHeapCount = 1;
        objectInfoBufferDesc.initialState = RHI::ResourceState::ShaderResource;
        objectInfoBufferDesc.stride = sizeof(GpuData::ObjectInfo);
        objectInfoBufferDesc.elementCount = a_maxObjectCount;
        objectInfoBufferDesc.size =
            objectInfoBufferDesc.stride * objectInfoBufferDesc.elementCount;
        objectInfoBufferDesc.alignment = alignof(GpuData::ObjectInfo);

        // ObjectInfoBuffer の作成
        RHI::BufferHandle& objectInfoBufferHandle =
            m_bufferHandles[static_cast<size_t>(WorldResourceType::ObjectInfoBuffer)];
        Result result = m_bufferManager->create_buffer(
            objectInfoBufferDesc, objectInfoBufferHandle);
        if (!result)
        {
            return result;
        }

        // ObjectInfoBuffer の uploader 作成
        result = m_bufferManager->create_slot_uploaders(
            m_bufferHandles[static_cast<size_t>(WorldResourceType::ObjectInfoBuffer)],
            1, m_objectInfoUploaders);
        if (!result)
        {
            return result;
        }
        if(m_objectInfoUploaders.size() != 1)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "ObjectInfoBuffer uploader was not created.");
        }

        // ObjectInfoBuffer の SRV 作成
        RHI::ViewDesc objectInfoBufferSrvDesc{};
        objectInfoBufferSrvDesc.name = "ObjectInfoBufferSRV";
        objectInfoBufferSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
        objectInfoBufferSrvDesc.bufferKind = RHI::BufferKind::Buffer;
        objectInfoBufferSrvDesc.bufferHandle = objectInfoBufferHandle;
        objectInfoBufferSrvDesc.firstElement = 0;
        objectInfoBufferSrvDesc.numElements = objectInfoBufferDesc.elementCount;
        objectInfoBufferSrvDesc.structureByteStride = objectInfoBufferDesc.stride;
        
        // ビューの作成
        RHI::ViewHandle& objectInfoBufferSrvHandle =
            m_viewHandles[static_cast<size_t>(WorldResourceType::ObjectInfoBuffer)];
        result = m_viewManager->create_view(objectInfoBufferSrvDesc,
            objectInfoBufferSrvHandle);
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
