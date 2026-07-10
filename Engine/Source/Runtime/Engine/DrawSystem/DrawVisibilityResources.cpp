#include "DrawVisibilityResources.h"

// === C++ includes ===
#include <utility>

namespace Cue::DrawSystem
{
    namespace
    {
        // 可視集合は View ごとに独立するため、各 visibility resource の frame slot を個別に更新する
        template <typename T>
        Result upload_slots(RHI::SlotUploader<T>& a_uploader, const std::vector<T>& a_values,
                            const char* a_errorMessage)
        {
            a_uploader.begin_frame();
            for (uint32_t index = 0; index < static_cast<uint32_t>(a_values.size()); ++index)
            {
                if (!a_uploader.push(index, a_values[index]))
                {
                    return Result::fail(Code::InvalidState, Severity::Error, a_errorMessage);
                }
            }
            if (!a_uploader.commit())
            {
                return Result::fail(Code::InvalidState, Severity::Error, a_errorMessage);
            }
            return Result::ok();
        }

        template <typename T>
        Result upload_single_slot(RHI::SlotUploader<T>& a_uploader, const T& a_value, const char* a_errorMessage)
        {
            a_uploader.begin_frame();
            if (!a_uploader.push(0, a_value) || !a_uploader.commit())
            {
                return Result::fail(Code::InvalidState, Severity::Error, a_errorMessage);
            }
            return Result::ok();
        }
    } // namespace

    DrawVisibilityResources::DrawVisibilityResources(
        RHI::IBufferManager* a_bufferManager,
        RHI::IViewManager* a_viewManager,
        uint32_t a_bufferCount,
        std::string a_name)
        : m_name(std::move(a_name))
        , m_bufferManager(a_bufferManager)
        , m_viewManager(a_viewManager)
        , m_bufferCount(a_bufferCount)
    {
    }

    Result DrawVisibilityResources::initialize(
        uint32_t a_maxObjectCount,
        uint32_t a_maxBatchCount,
        uint32_t a_maxObjectIndexCount)
    {
        if (m_bufferManager == nullptr || m_viewManager == nullptr || m_bufferCount == 0 || m_name.empty() ||
            a_maxObjectCount == 0 || a_maxBatchCount == 0 || a_maxObjectIndexCount == 0)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "DrawVisibilityResources initialization is invalid.");
        }

        // frustum / occlusion culling の出力は camera で変わるため、RenderObject と counter を View ごとに所有する
        RHI::BufferDesc renderObjectDesc{};
        renderObjectDesc.name = m_name + "RenderObjectBuffer";
        renderObjectDesc.type = RHI::BufferType::UnorderedAccess;
        renderObjectDesc.defaultHeapCount = 1;
        renderObjectDesc.uploadHeapCount = m_bufferCount;
        renderObjectDesc.initialState = RHI::ResourceState::UnorderedAccess;
        renderObjectDesc.stride = sizeof(GpuData::RenderObject);
        renderObjectDesc.elementCount = a_maxObjectCount;
        renderObjectDesc.size = renderObjectDesc.stride * renderObjectDesc.elementCount;
        renderObjectDesc.alignment = alignof(GpuData::RenderObject);
        Result result = m_bufferManager->create_buffer(renderObjectDesc, m_renderObjectBuffer);
        if (!result)
        {
            return result;
        }
        result = m_bufferManager->create_slot_uploaders(m_renderObjectBuffer, m_bufferCount, m_renderObjectUploaders);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc renderObjectViewDesc{};
        renderObjectViewDesc.name = m_name + "RenderObjectBufferUAV";
        renderObjectViewDesc.type = RHI::ViewType::UnorderedAccessBuffer;
        renderObjectViewDesc.bufferKind = RHI::BufferKind::Buffer;
        renderObjectViewDesc.bufferHandle = m_renderObjectBuffer;
        renderObjectViewDesc.numElements = a_maxObjectCount;
        renderObjectViewDesc.structureByteStride = sizeof(GpuData::RenderObject);
        result = m_viewManager->create_view(renderObjectViewDesc, m_renderObjectBufferUav);
        if (!result)
        {
            return result;
        }

        RHI::BufferDesc visibleCountDesc{};
        visibleCountDesc.name = m_name + "VisibleObjectCountBuffer";
        visibleCountDesc.type = RHI::BufferType::Raw;
        visibleCountDesc.defaultHeapCount = 1;
        visibleCountDesc.uploadHeapCount = m_bufferCount;
        visibleCountDesc.initialState = RHI::ResourceState::UnorderedAccess;
        visibleCountDesc.stride = sizeof(uint32_t);
        visibleCountDesc.elementCount = 1;
        visibleCountDesc.size = sizeof(uint32_t);
        visibleCountDesc.alignment = alignof(uint32_t);
        result = m_bufferManager->create_buffer(visibleCountDesc, m_visibleObjectCountBuffer);
        if (!result)
        {
            return result;
        }
        result = m_bufferManager->create_slot_uploaders(
            m_visibleObjectCountBuffer, m_bufferCount, m_visibleObjectCountUploaders);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc visibleCountViewDesc{};
        visibleCountViewDesc.name = m_name + "VisibleObjectCountBufferUAV";
        visibleCountViewDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
        visibleCountViewDesc.bufferKind = RHI::BufferKind::Buffer;
        visibleCountViewDesc.bufferHandle = m_visibleObjectCountBuffer;
        visibleCountViewDesc.numElements = 1;
        result = m_viewManager->create_view(visibleCountViewDesc, m_visibleObjectCountBufferUav);
        if (!result)
        {
            return result;
        }

        // culling 後の可視集合から indirect argument を生成できるよう、command 系も View 固有に保つ
        RHI::BufferDesc commandDesc{};
        commandDesc.name = m_name + "StaticMeshIndirectCommandBuffer";
        commandDesc.type = RHI::BufferType::Structured;
        commandDesc.defaultHeapCount = 0;
        commandDesc.uploadHeapCount = m_bufferCount;
        commandDesc.initialState = RHI::ResourceState::IndirectArgument;
        commandDesc.stride = sizeof(GpuData::IndirectCommand);
        commandDesc.elementCount = a_maxBatchCount;
        commandDesc.size = commandDesc.stride * commandDesc.elementCount;
        commandDesc.alignment = alignof(GpuData::IndirectCommand);
        result = m_bufferManager->create_buffer(commandDesc, m_staticMeshIndirectCommandBuffer);
        if (!result)
        {
            return result;
        }
        result = m_bufferManager->create_slot_uploaders(
            m_staticMeshIndirectCommandBuffer, m_bufferCount, m_staticMeshIndirectCommandUploaders);
        if (!result)
        {
            return result;
        }

        RHI::BufferDesc commandCountDesc{};
        commandCountDesc.name = m_name + "StaticMeshIndirectCommandCountBuffer";
        commandCountDesc.type = RHI::BufferType::Raw;
        commandCountDesc.defaultHeapCount = 0;
        commandCountDesc.uploadHeapCount = m_bufferCount;
        commandCountDesc.initialState = RHI::ResourceState::IndirectArgument;
        commandCountDesc.stride = sizeof(uint32_t);
        commandCountDesc.elementCount = 1;
        commandCountDesc.size = sizeof(uint32_t);
        commandCountDesc.alignment = alignof(uint32_t);
        result = m_bufferManager->create_buffer(commandCountDesc, m_staticMeshIndirectCommandCountBuffer);
        if (!result)
        {
            return result;
        }
        result = m_bufferManager->create_slot_uploaders(
            m_staticMeshIndirectCommandCountBuffer, m_bufferCount, m_staticMeshIndirectCommandCountUploaders);
        if (!result)
        {
            return result;
        }

        RHI::BufferDesc objectIndexDesc{};
        objectIndexDesc.name = m_name + "StaticMeshObjectIndexBuffer";
        objectIndexDesc.type = RHI::BufferType::Structured;
        objectIndexDesc.defaultHeapCount = 0;
        objectIndexDesc.uploadHeapCount = m_bufferCount;
        objectIndexDesc.initialState = RHI::ResourceState::ShaderResource;
        objectIndexDesc.stride = sizeof(uint32_t);
        objectIndexDesc.elementCount = a_maxObjectIndexCount;
        objectIndexDesc.size = objectIndexDesc.stride * objectIndexDesc.elementCount;
        objectIndexDesc.alignment = alignof(uint32_t);
        result = m_bufferManager->create_buffer(objectIndexDesc, m_staticMeshObjectIndexBuffer);
        if (!result)
        {
            return result;
        }
        result = m_bufferManager->create_slot_uploaders(
            m_staticMeshObjectIndexBuffer, m_bufferCount, m_staticMeshObjectIndexUploaders);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc objectIndexViewDesc{};
        objectIndexViewDesc.name = m_name + "StaticMeshObjectIndexBufferSRV";
        objectIndexViewDesc.type = RHI::ViewType::ShaderResourceBuffer;
        objectIndexViewDesc.bufferKind = RHI::BufferKind::Buffer;
        objectIndexViewDesc.bufferHandle = m_staticMeshObjectIndexBuffer;
        objectIndexViewDesc.numElements = a_maxObjectIndexCount;
        objectIndexViewDesc.structureByteStride = sizeof(uint32_t);
        result = m_viewManager->create_view(objectIndexViewDesc, m_staticMeshObjectIndexBufferSrv);
        if (!result)
        {
            return result;
        }

        m_maxObjectCount = a_maxObjectCount;
        m_maxBatchCount = a_maxBatchCount;
        m_maxObjectIndexCount = a_maxObjectIndexCount;
        return Result::ok();
    }

    Result DrawVisibilityResources::upload_visibility(uint32_t a_bufferIndex, const DrawFrameData& a_frameData)
    {
        if (a_bufferIndex >= m_bufferCount ||
            m_staticMeshIndirectCommandUploaders.size() != m_bufferCount ||
            m_staticMeshIndirectCommandCountUploaders.size() != m_bufferCount ||
            m_staticMeshObjectIndexUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawVisibilityResources uploaders are not initialized.");
        }
        if (a_frameData.staticMeshIndirectCommands.size() > m_maxBatchCount ||
            a_frameData.staticMeshObjectIndices.size() > m_maxObjectIndexCount ||
            a_frameData.staticMeshBatches.size() != a_frameData.staticMeshIndirectCommands.size() ||
            a_frameData.staticMeshBatchCount != a_frameData.staticMeshBatches.size() ||
            a_frameData.indirectCommandCount != a_frameData.staticMeshIndirectCommands.size())
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawVisibility frame data is invalid.");
        }

        // 現在は CPU batching の結果を転送し、将来は culling pass が同じ buffer を GPU 側で更新する
        Result result = upload_slots(
            m_staticMeshIndirectCommandUploaders[a_bufferIndex],
            a_frameData.staticMeshIndirectCommands,
            "Failed to upload StaticMeshIndirectCommandBuffer.");
        if (!result)
        {
            return result;
        }
        result = upload_slots(
            m_staticMeshObjectIndexUploaders[a_bufferIndex],
            a_frameData.staticMeshObjectIndices,
            "Failed to upload StaticMeshObjectIndexBuffer.");
        if (!result)
        {
            return result;
        }
        return upload_single_slot(
            m_staticMeshIndirectCommandCountUploaders[a_bufferIndex],
            a_frameData.indirectCommandCount,
            "Failed to upload StaticMeshIndirectCommandCountBuffer.");
    }

    RHI::BufferHandle DrawVisibilityResources::render_object_buffer_handle() const noexcept
    {
        return m_renderObjectBuffer;
    }

    RHI::BufferHandle DrawVisibilityResources::visible_object_count_buffer_handle() const noexcept
    {
        return m_visibleObjectCountBuffer;
    }

    RHI::ViewHandle DrawVisibilityResources::render_object_buffer_uav_handle() const noexcept
    {
        return m_renderObjectBufferUav;
    }

    RHI::ViewHandle DrawVisibilityResources::visible_object_count_buffer_uav_handle() const noexcept
    {
        return m_visibleObjectCountBufferUav;
    }

    RHI::BufferHandle DrawVisibilityResources::static_mesh_indirect_command_buffer_handle() const noexcept
    {
        return m_staticMeshIndirectCommandBuffer;
    }

    RHI::BufferHandle DrawVisibilityResources::static_mesh_indirect_command_count_buffer_handle() const noexcept
    {
        return m_staticMeshIndirectCommandCountBuffer;
    }

    RHI::BufferHandle DrawVisibilityResources::static_mesh_object_index_buffer_handle() const noexcept
    {
        return m_staticMeshObjectIndexBuffer;
    }

    RHI::ViewHandle DrawVisibilityResources::static_mesh_object_index_buffer_srv_handle() const noexcept
    {
        return m_staticMeshObjectIndexBufferSrv;
    }

    uint64_t DrawVisibilityResources::static_mesh_indirect_command_buffer_byte_size() const noexcept
    {
        return static_cast<uint64_t>(m_maxBatchCount) * sizeof(GpuData::IndirectCommand);
    }

    uint64_t DrawVisibilityResources::static_mesh_indirect_command_count_buffer_byte_size() const noexcept
    {
        return sizeof(uint32_t);
    }

    uint64_t DrawVisibilityResources::static_mesh_object_index_buffer_byte_size() const noexcept
    {
        return static_cast<uint64_t>(m_maxObjectIndexCount) * sizeof(uint32_t);
    }
} // namespace Cue::DrawSystem
