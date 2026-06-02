#include <ParticleSystem/ParticleResources.h>

namespace Cue::ParticleSystem
{
    Result ParticleResources::create_frame_buffer()
    {
        constexpr uint32_t k_constantBufferAlignment = 256;

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "ParticleFrameBuffer";
        bufferDesc.type = RHI::BufferType::Constant;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_bufferCount;
        bufferDesc.initialState = RHI::ResourceState::ShaderResource;
        bufferDesc.stride = sizeof(GpuData::ParticleFrameGpu);
        bufferDesc.elementCount = 1;
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = k_constantBufferAlignment;

        RHI::BufferHandle& handle =
            m_bufferHandles[static_cast<size_t>(ParticleResourceType::FrameBuffer)];
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
                "Particle frame uploader was not created.");
        }

        return Result::ok();
    }

    Result ParticleResources::create_emitter_buffer(uint32_t a_maxEmitterCount)
    {
        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "ParticleEmitterBuffer";
        bufferDesc.type = RHI::BufferType::Structured;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = m_bufferCount;
        bufferDesc.initialState = RHI::ResourceState::ShaderResource;
        bufferDesc.stride = sizeof(GpuData::ParticleEmitterGpu);
        bufferDesc.elementCount = a_maxEmitterCount;
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = alignof(GpuData::ParticleEmitterGpu);

        RHI::BufferHandle& handle =
            m_bufferHandles[static_cast<size_t>(ParticleResourceType::EmitterBuffer)];
        Result result = m_bufferManager->create_buffer(bufferDesc, handle);
        if (!result)
        {
            return result;
        }

        result = m_bufferManager->create_slot_uploaders(
            handle, m_bufferCount, m_emitterUploaders);
        if (!result)
        {
            return result;
        }
        if (m_emitterUploaders.size() != m_bufferCount)
        {
            return Result::fail(Code::InternalError, Severity::Fatal,
                "Particle emitter uploader was not created.");
        }

        return Result::ok();
    }

    Result ParticleResources::create_particle_buffer(uint32_t a_maxParticleCount)
    {
        m_maxParticleCount = a_maxParticleCount;

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "ParticleBuffer";
        bufferDesc.type = RHI::BufferType::UnorderedAccess;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = 0;
        bufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
        bufferDesc.stride = sizeof(GpuData::ParticleGpu);
        bufferDesc.elementCount = a_maxParticleCount;
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = alignof(GpuData::ParticleGpu);

        RHI::BufferHandle& handle =
            m_bufferHandles[static_cast<size_t>(ParticleResourceType::ParticleBuffer)];
        Result result = m_bufferManager->create_buffer(bufferDesc, handle);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc uavDesc{};
        uavDesc.name = "ParticleBufferUAV";
        uavDesc.type = RHI::ViewType::UnorderedAccessBuffer;
        uavDesc.bufferKind = RHI::BufferKind::Buffer;
        uavDesc.bufferHandle = handle;
        uavDesc.firstElement = 0;
        uavDesc.numElements = bufferDesc.elementCount;
        uavDesc.structureByteStride = bufferDesc.stride;

        RHI::ViewHandle& uavHandle =
            m_viewHandles[static_cast<size_t>(ParticleResourceType::ParticleBuffer)];
        return m_viewManager->create_view(uavDesc, uavHandle);
    }

    Result ParticleResources::create_trail_buffer(
        uint32_t a_maxParticleCount,
        uint32_t a_maxTrailSegmentCount)
    {
        m_maxTrailSegmentCount = a_maxTrailSegmentCount;

        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "ParticleTrailBuffer";
        bufferDesc.type = RHI::BufferType::UnorderedAccess;
        bufferDesc.defaultHeapCount = 1;
        bufferDesc.uploadHeapCount = 0;
        bufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
        bufferDesc.stride = sizeof(GpuData::ParticleTrailPointGpu);
        bufferDesc.elementCount = a_maxParticleCount * a_maxTrailSegmentCount;
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = alignof(GpuData::ParticleTrailPointGpu);

        RHI::BufferHandle& handle =
            m_bufferHandles[static_cast<size_t>(ParticleResourceType::TrailBuffer)];
        Result result = m_bufferManager->create_buffer(bufferDesc, handle);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc uavDesc{};
        uavDesc.name = "ParticleTrailBufferUAV";
        uavDesc.type = RHI::ViewType::UnorderedAccessBuffer;
        uavDesc.bufferKind = RHI::BufferKind::Buffer;
        uavDesc.bufferHandle = handle;
        uavDesc.firstElement = 0;
        uavDesc.numElements = bufferDesc.elementCount;
        uavDesc.structureByteStride = bufferDesc.stride;

        RHI::ViewHandle& uavHandle =
            m_viewHandles[static_cast<size_t>(ParticleResourceType::TrailBuffer)];
        return m_viewManager->create_view(uavDesc, uavHandle);
    }
} // namespace Cue::ParticleSystem
