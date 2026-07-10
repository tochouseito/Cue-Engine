#include "DrawSceneResources.h"

// === Engine includes ===
#include "DrawScene.h"

// === C++ includes ===
#include <limits>
#include <string>

namespace Cue::DrawSystem
{
    namespace
    {
        // Scene 入力は View に依存しないため、同じ buffer 作成手順で SRV と frame uploader を揃える
        template <typename T>
        Result create_structured_srv_buffer(
            RHI::IBufferManager& a_bufferManager,
            RHI::IViewManager& a_viewManager,
            uint32_t a_bufferCount,
            const std::string& a_name,
            uint32_t a_elementCount,
            RHI::BufferHandle& a_outBuffer,
            std::vector<RHI::SlotUploader<T>>& a_outUploaders,
            RHI::ViewHandle& a_outView)
        {
            RHI::BufferDesc bufferDesc{};
            bufferDesc.name = a_name;
            bufferDesc.type = RHI::BufferType::Structured;
            bufferDesc.defaultHeapCount = 1;
            bufferDesc.uploadHeapCount = a_bufferCount;
            bufferDesc.initialState = RHI::ResourceState::ShaderResource;
            bufferDesc.stride = sizeof(T);
            bufferDesc.elementCount = a_elementCount;
            bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
            bufferDesc.alignment = alignof(T);

            Result result = a_bufferManager.create_buffer(bufferDesc, a_outBuffer);
            if (!result)
            {
                return result;
            }

            result = a_bufferManager.create_slot_uploaders(a_outBuffer, a_bufferCount, a_outUploaders);
            if (!result)
            {
                return result;
            }
            if (a_outUploaders.size() != a_bufferCount)
            {
                return Result::fail(Code::InternalError, Severity::Fatal, "Scene buffer uploaders were not created.");
            }

            RHI::ViewDesc viewDesc{};
            viewDesc.name = a_name + "SRV";
            viewDesc.type = RHI::ViewType::ShaderResourceBuffer;
            viewDesc.bufferKind = RHI::BufferKind::Buffer;
            viewDesc.bufferHandle = a_outBuffer;
            viewDesc.firstElement = 0;
            viewDesc.numElements = a_elementCount;
            viewDesc.structureByteStride = sizeof(T);
            return a_viewManager.create_view(viewDesc, a_outView);
        }

        template <typename T>
        Result upload_slots(RHI::SlotUploader<T>& a_uploader, const std::vector<T>& a_values,
                            const char* a_errorMessage)
        {
            // frame slot を空にしてから詰め直し、前 frame の trailing data を描画へ残さない
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
    } // namespace

    DrawSceneResources::DrawSceneResources(
        RHI::IBufferManager* a_bufferManager,
        RHI::IViewManager* a_viewManager,
        uint32_t a_bufferCount)
        : m_bufferManager(a_bufferManager)
        , m_viewManager(a_viewManager)
        , m_bufferCount(a_bufferCount)
    {
    }

    Result DrawSceneResources::initialize(uint32_t a_maxObjectCount, uint32_t a_maxCellCount)
    {
        if (m_bufferManager == nullptr || m_viewManager == nullptr || m_bufferCount == 0 ||
            a_maxObjectCount == 0 || a_maxCellCount == 0)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "DrawSceneResources initialization is invalid.");
        }

        // culling と forward pass が同じ object index を基準に参照するため、Scene 入力は一組だけ確保する
        Result result = create_structured_srv_buffer(
            *m_bufferManager, *m_viewManager, m_bufferCount, "RenderableInfoBuffer", a_maxObjectCount,
            m_renderableInfoBuffer, m_renderableInfoUploaders, m_renderableInfoBufferSrv);
        if (!result)
        {
            return result;
        }

        result = create_structured_srv_buffer(
            *m_bufferManager, *m_viewManager, m_bufferCount, "TransformBuffer", a_maxObjectCount,
            m_transformBuffer, m_transformUploaders, m_transformBufferSrv);
        if (!result)
        {
            return result;
        }

        result = create_structured_srv_buffer(
            *m_bufferManager, *m_viewManager, m_bufferCount, "MaterialBuffer", a_maxObjectCount,
            m_materialBuffer, m_materialUploaders, m_materialBufferSrv);
        if (!result)
        {
            return result;
        }

        result = create_structured_srv_buffer(
            *m_bufferManager, *m_viewManager, m_bufferCount, "RenderCellBuffer", a_maxCellCount,
            m_renderCellBuffer, m_renderCellUploaders, m_renderCellBufferSrv);
        if (!result)
        {
            return result;
        }

        m_maxObjectCount = a_maxObjectCount;
        m_maxCellCount = a_maxCellCount;
        return Result::ok();
    }

    Result DrawSceneResources::upload_draw_scene(uint32_t a_bufferIndex, const DrawScene& a_scene)
    {
        if (a_bufferIndex >= m_bufferCount || m_renderableInfoUploaders.size() != m_bufferCount ||
            m_transformUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawSceneResources uploaders are not initialized.");
        }

        // camera は DrawViewResources へ分離し、ここでは全 View で一致する Scene 配列だけを更新する
        const size_t objectCount = a_scene.object_count();
        const std::vector<GpuData::RenderableInfo>& renderableInfos = a_scene.renderable_infos();
        const std::vector<GpuData::ObjectTransformGpu>& transforms = a_scene.transforms();
        if (renderableInfos.size() != objectCount || transforms.size() != objectCount)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawScene arrays are not aligned.");
        }
        if (objectCount > m_maxObjectCount || objectCount > (std::numeric_limits<uint32_t>::max)())
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawScene object count exceeds capacity.");
        }

        Result result = upload_slots(
            m_renderableInfoUploaders[a_bufferIndex], renderableInfos, "Failed to upload RenderableInfoBuffer.");
        if (!result)
        {
            return result;
        }
        return upload_slots(m_transformUploaders[a_bufferIndex], transforms, "Failed to upload TransformBuffer.");
    }

    RHI::BufferHandle DrawSceneResources::renderable_info_buffer_handle() const noexcept
    {
        return m_renderableInfoBuffer;
    }

    RHI::BufferHandle DrawSceneResources::transform_buffer_handle() const noexcept
    {
        return m_transformBuffer;
    }

    RHI::BufferHandle DrawSceneResources::material_buffer_handle() const noexcept
    {
        return m_materialBuffer;
    }

    RHI::BufferHandle DrawSceneResources::render_cell_buffer_handle() const noexcept
    {
        return m_renderCellBuffer;
    }

    RHI::ViewHandle DrawSceneResources::transform_buffer_srv_handle() const noexcept
    {
        return m_transformBufferSrv;
    }

    RHI::ViewHandle DrawSceneResources::renderable_info_buffer_srv_handle() const noexcept
    {
        return m_renderableInfoBufferSrv;
    }

    RHI::ViewHandle DrawSceneResources::material_buffer_srv_handle() const noexcept
    {
        return m_materialBufferSrv;
    }

    RHI::ViewHandle DrawSceneResources::render_cell_buffer_srv_handle() const noexcept
    {
        return m_renderCellBufferSrv;
    }

    uint64_t DrawSceneResources::renderable_info_buffer_byte_size() const noexcept
    {
        return static_cast<uint64_t>(m_maxObjectCount) * sizeof(GpuData::RenderableInfo);
    }

    uint64_t DrawSceneResources::transform_buffer_byte_size() const noexcept
    {
        return static_cast<uint64_t>(m_maxObjectCount) * sizeof(GpuData::ObjectTransformGpu);
    }
} // namespace Cue::DrawSystem
