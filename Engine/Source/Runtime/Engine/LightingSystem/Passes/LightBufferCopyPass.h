#pragma once

/// ****************************************************************************
/// Copy light upload buffers to default buffers
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace Cue::LightingSystem
{
    class LightUploadCopyVersion final
    {
    public:
        explicit LightUploadCopyVersion(const uint64_t& a_version) noexcept
            : m_version(a_version)
        {}

        [[nodiscard]] bool should_copy() const noexcept
        {
            return m_copiedVersion != m_version;
        }

        void mark_copied() noexcept
        {
            m_copiedVersion = m_version;
        }

    private:
        const uint64_t& m_version;
        uint64_t m_copiedVersion = (std::numeric_limits<uint64_t>::max)();
    };

    class LightBufferCopyPass final : public RHI::FrameGraphPass
    {
    public:
        LightBufferCopyPass(
            std::string name,
            RHI::BufferHandle bufferHandle,
            uint64_t copyByteSize,
            const uint64_t& a_uploadVersion)
            : m_name(std::move(name))
            , m_bufferHandle(bufferHandle)
            , m_copyByteSize(copyByteSize)
            , m_copyVersion(a_uploadVersion)
        {}

        const char* name() const noexcept override
        {
            return m_name.c_str();
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Copy;
        }

        uint32_t queue_lane() const noexcept override
        {
            return 1u;
        }

        bool is_enabled(uint32_t) const noexcept override
        {
            return m_bufferHandle.valid() && m_copyByteSize != 0 && m_copyVersion.should_copy();
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            return builder.read_buffer(m_bufferHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_bufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (!m_bufferHandle.valid() || m_copyByteSize == 0)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_bufferHandle;
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_bufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize = m_copyByteSize;

            Result result = commandContext->copy_buffer_region(region);
            if (result)
            {
                m_copyVersion.mark_copied();
            }
        }

    private:
        std::string m_name{};
        RHI::BufferHandle m_bufferHandle{};
        uint64_t m_copyByteSize = 0;
        mutable LightUploadCopyVersion m_copyVersion;
    };
}
