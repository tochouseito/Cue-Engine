#include "ShadowResources.h"

namespace Cue::ShadowSystem
{
    namespace
    {
        constexpr uint32_t k_constantBufferAlignment = 256;
    }

    ShadowResources::ShadowResources(
        RHI::IBufferManager* a_bufferManager,
        RHI::IViewManager* a_viewManager,
        uint32_t a_bufferCount)
        : m_bufferManager(a_bufferManager)
        , m_viewManager(a_viewManager)
        , m_bufferCount(a_bufferCount)
    {}

    [[nodiscard]] Result ShadowResources::create_spot_shadow_frame_buffer()
    {
        if (m_bufferManager == nullptr || m_viewManager == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Shadow resource managers are not initialized.");
        }

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "SpotShadowFrameBuffer";
        bufferDesc.type = RHI::BufferType::Structured;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_bufferCount;
        bufferDesc.elementCount = GpuData::k_maxSpotShadowCount;
        bufferDesc.stride = sizeof(GpuData::SpotShadowFrameGpu);
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = alignof(GpuData::SpotShadowFrameGpu);
        bufferDesc.initialState = RHI::ResourceState::Common;

        RHI::BufferHandle& handle =
            m_buffers[static_cast<size_t>(
                ShadowResourceType::SpotShadowFrameBuffer)];
        Result result = m_bufferManager->create_buffer(bufferDesc, handle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            handle,
            m_bufferCount,
            m_spotShadowFrameUploaders);
        if (!result)
        {
            return result;
        }
        if (m_spotShadowFrameUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal,
                "SpotShadowFrameBuffer uploader was not created.");
        }

        return Result::ok();
    }

    [[nodiscard]] Result ShadowResources::create_directional_shadow_frame_buffer()
    {
        if (m_bufferManager == nullptr || m_viewManager == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Shadow resource managers are not initialized.");
        }

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "DirectionalShadowFrameBuffer";
        bufferDesc.type = RHI::BufferType::Constant;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_bufferCount;
        bufferDesc.elementCount = 1;
        bufferDesc.stride = sizeof(GpuData::DirectionalShadowFrameGpu);
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = k_constantBufferAlignment;
        bufferDesc.initialState = RHI::ResourceState::Common;

        RHI::BufferHandle& handle =
            m_buffers[static_cast<size_t>(
                ShadowResourceType::DirectionalShadowFrameBuffer)];
        Result result = m_bufferManager->create_buffer(bufferDesc, handle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            handle,
            m_bufferCount,
            m_directionalShadowFrameUploaders);
        if (!result)
        {
            return result;
        }
        if (m_directionalShadowFrameUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal,
                "DirectionalShadowFrameBuffer uploader was not created.");
        }

        return Result::ok();
    }

    [[nodiscard]] Result ShadowResources::create_point_shadow_face_buffer()
    {
        if (m_bufferManager == nullptr || m_viewManager == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Shadow resource managers are not initialized.");
        }

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "PointShadowFaceBuffer";
        bufferDesc.type = RHI::BufferType::Structured;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_bufferCount;
        bufferDesc.elementCount = GpuData::k_pointShadowFaceCount;
        bufferDesc.stride = sizeof(GpuData::PointShadowFaceGpu);
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = alignof(GpuData::PointShadowFaceGpu);
        bufferDesc.initialState = RHI::ResourceState::Common;

        RHI::BufferHandle& handle =
            m_buffers[static_cast<size_t>(
                ShadowResourceType::PointShadowFaceBuffer)];
        Result result = m_bufferManager->create_buffer(bufferDesc, handle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            handle,
            m_bufferCount,
            m_pointShadowFaceUploaders);
        if (!result)
        {
            return result;
        }
        if (m_pointShadowFaceUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal,
                "PointShadowFaceBuffer uploader was not created.");
        }

        return Result::ok();
    }
}
