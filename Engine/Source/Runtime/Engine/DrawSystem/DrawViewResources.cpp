#include "DrawViewResources.h"

// === C++ includes ===
#include <utility>

namespace Cue::DrawSystem
{
    DrawViewResources::DrawViewResources(
        RHI::IBufferManager* a_bufferManager,
        uint32_t a_bufferCount,
        std::string a_name)
        : m_name(std::move(a_name))
        , m_bufferManager(a_bufferManager)
        , m_bufferCount(a_bufferCount)
    {
    }

    Result DrawViewResources::initialize()
    {
        if (m_bufferManager == nullptr || m_bufferCount == 0 || m_name.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "DrawViewResources initialization is invalid.");
        }

        // ViewProjection は camera ごとに異なるため、Scene input と共有せず D3D12 の CBV alignment で確保する
        constexpr uint32_t k_constantBufferAlignment = 256;
        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = m_name + "ViewProjectionBuffer";
        bufferDesc.type = RHI::BufferType::Constant;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_bufferCount;
        bufferDesc.initialState = RHI::ResourceState::Common;
        bufferDesc.stride = sizeof(GpuData::ViewProjectionGpu);
        bufferDesc.elementCount = 1;
        bufferDesc.size = bufferDesc.stride;
        bufferDesc.alignment = k_constantBufferAlignment;

        Result result = m_bufferManager->create_buffer(bufferDesc, m_viewProjectionBuffer);
        if (!result)
        {
            return result;
        }
        result = m_bufferManager->create_slot_uploaders(
            m_viewProjectionBuffer, m_bufferCount, m_viewProjectionUploaders);
        if (!result)
        {
            return result;
        }
        if (m_viewProjectionUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal, "ViewProjectionBuffer uploaders were not created.");
        }
        return Result::ok();
    }

    Result DrawViewResources::upload_view(uint32_t a_bufferIndex, const RenderView& a_view)
    {
        if (a_bufferIndex >= m_bufferCount || m_viewProjectionUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InvalidState, Severity::Error, "DrawViewResources uploaders are not initialized.");
        }

        // GameView と DebugView が同一 frame に異なる camera を使えるよう、View ごとの upload slot を更新する
        RHI::SlotUploader<GpuData::ViewProjectionGpu>& uploader = m_viewProjectionUploaders[a_bufferIndex];
        uploader.begin_frame();
        if (!uploader.push(0, make_view_projection_gpu(a_view)) || !uploader.commit())
        {
            return Result::fail(Code::InvalidState, Severity::Error, "Failed to upload ViewProjectionBuffer.");
        }
        return Result::ok();
    }

    RHI::BufferHandle DrawViewResources::view_projection_buffer_handle() const noexcept
    {
        return m_viewProjectionBuffer;
    }

    uint64_t DrawViewResources::view_projection_buffer_byte_size() const noexcept
    {
        return sizeof(GpuData::ViewProjectionGpu);
    }
} // namespace Cue::DrawSystem
