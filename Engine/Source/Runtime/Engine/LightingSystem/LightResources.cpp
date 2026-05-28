// LightResources の実装点を分け、ライト用 GPU バッファ管理を lighting system 内へ閉じる

#include <LightingSystem/LightResources.h>

namespace Cue::LightingSystem
{
    namespace
    {
        constexpr uint32_t k_constantBufferAlignment = 256;

        template <typename T>
        Result create_structured_light_buffer(
            RHI::IBufferManager& a_bufferManager,
            std::string_view a_name,
            uint32_t a_elementCount,
            uint32_t a_bufferCount,
            RHI::BufferHandle& a_outHandle,
            std::vector<RHI::SlotUploader<T>>& a_outUploaders)
        {
            RHI::BufferDesc bufferDesc{};
            bufferDesc.name = std::string(a_name);
            bufferDesc.type = RHI::BufferType::Structured;
            bufferDesc.defaultHeapCount = 1;
            bufferDesc.uploadHeapCount = a_bufferCount;
            bufferDesc.initialState = RHI::ResourceState::ShaderResource;
            bufferDesc.stride = sizeof(T);
            bufferDesc.elementCount = a_elementCount;
            bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
            bufferDesc.alignment = alignof(T);

            Result result = a_bufferManager.create_buffer(bufferDesc, a_outHandle);
            if (!result)
            {
                return result;
            }

            result = a_bufferManager.create_slot_uploaders(
                a_outHandle, a_bufferCount, a_outUploaders);
            if (!result)
            {
                return result;
            }
            if (a_outUploaders.size() != a_bufferCount)
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

    Result LightResources::create_directional_light_buffer(
        uint32_t a_maxLightCount)
    {
        if (m_bufferManager == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "LightResources buffer manager is not initialized.");
        }

        return create_structured_light_buffer(
            *m_bufferManager,
            "DirectionalLightBuffer",
            a_maxLightCount,
            m_bufferCount,
            m_bufferHandles[static_cast<size_t>(
                LightResourceType::DirectionalLightBuffer)],
            m_directionalLightUploaders);
    }

    Result LightResources::create_point_light_buffer(uint32_t a_maxLightCount)
    {
        if (m_bufferManager == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "LightResources buffer manager is not initialized.");
        }

        return create_structured_light_buffer(
            *m_bufferManager,
            "PointLightBuffer",
            a_maxLightCount,
            m_bufferCount,
            m_bufferHandles[static_cast<size_t>(
                LightResourceType::PointLightBuffer)],
            m_pointLightUploaders);
    }

    Result LightResources::create_spot_light_buffer(uint32_t a_maxLightCount)
    {
        if (m_bufferManager == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "LightResources buffer manager is not initialized.");
        }

        return create_structured_light_buffer(
            *m_bufferManager,
            "SpotLightBuffer",
            a_maxLightCount,
            m_bufferCount,
            m_bufferHandles[static_cast<size_t>(
                LightResourceType::SpotLightBuffer)],
            m_spotLightUploaders);
    }
}
