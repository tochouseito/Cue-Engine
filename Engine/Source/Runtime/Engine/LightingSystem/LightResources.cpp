#include "LightingSystem/LightResources.h"

namespace Cue::LightingSystem
{
    namespace
    {
        constexpr uint32_t k_constantBufferAlignment = 256;

        template <typename T>
        Result create_structured_light_buffer(
            RHI::IBufferManager& bufferManager,
            std::string_view name,
            uint32_t elementCount,
            uint32_t bufferCount,
            RHI::BufferHandle& outHandle,
            std::vector<RHI::SlotUploader<T>>& outUploaders)
        {
            RHI::BufferDesc bufferDesc{};
            bufferDesc.name = std::string(name);
            bufferDesc.type = RHI::BufferType::Structured;
            bufferDesc.defaultHeapCount = 1;
            bufferDesc.uploadHeapCount = bufferCount;
            bufferDesc.initialState = RHI::ResourceState::ShaderResource;
            bufferDesc.stride = sizeof(T);
            bufferDesc.elementCount = elementCount;
            bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
            bufferDesc.alignment = alignof(T);

            Result result = bufferManager.create_buffer(bufferDesc, outHandle);
            if (!result)
            {
                return result;
            }

            result = bufferManager.create_slot_uploaders(
                outHandle, bufferCount, outUploaders);
            if (!result)
            {
                return result;
            }
            if (outUploaders.size() != bufferCount)
            {
                return Result::fail(Code::InternalError, Severity::Fatal,
                    "Light buffer uploader was not created.");
            }

            return Result::ok();
        }
    }

    Result LightResources::create_frame_buffer()
    {
        if (m_bufferManager == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "LightResources buffer manager is not initialized.");
        }

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "LightFrameBuffer";
        bufferDesc.type = RHI::BufferType::Constant;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_bufferCount;
        bufferDesc.initialState = RHI::ResourceState::Common;
        bufferDesc.stride = sizeof(GpuData::LightFrameGpu);
        bufferDesc.elementCount = 1;
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = k_constantBufferAlignment;

        RHI::BufferHandle& handle =
            m_bufferHandles[static_cast<size_t>(LightResourceType::FrameBuffer)];
        Result result = m_bufferManager->create_buffer(bufferDesc, handle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            handle, m_bufferCount, m_frameUploaders);
        if (!result)
        {
            return result;
        }
        if (m_frameUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal,
                "LightFrameBuffer uploader was not created.");
        }

        return Result::ok();
    }

    Result LightResources::create_point_light_buffer(uint32_t maxLightCount)
    {
        if (m_bufferManager == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "LightResources buffer manager is not initialized.");
        }

        return create_structured_light_buffer(
            *m_bufferManager,
            "PointLightBuffer",
            maxLightCount,
            m_bufferCount,
            m_bufferHandles[static_cast<size_t>(
                LightResourceType::PointLightBuffer)],
            m_pointLightUploaders);
    }
}
