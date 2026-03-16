#pragma once
#include "BufferManager.h"
#include "SlotUploader.h"
#include <Native/EngineNativeStruct.h>

namespace Cue::GraphicsCore
{
    struct TransformSlotHandle final
    {
        static constexpr uint32_t k_invalidSlot = (std::numeric_limits<uint32_t>::max)();

        uint32_t slotIndex = k_invalidSlot;

        [[nodiscard]] bool valid() const noexcept
        {
            return slotIndex != k_invalidSlot;
        }
    };

    struct TransformBufferPoolDesc final
    {
        uint32_t slotCapacity = 1024;
        uint32_t bufferingCount = 0;
    };

    class TransformBufferPool final
    {
    public:
        Result initialize(const TransformBufferPoolDesc& desc, IBufferManager& bufferManager);
        Result shutdown();

        Result allocate_slot(TransformSlotHandle& outHandle);
        Result begin_frame(uint32_t bufferIndex);
        Result push(uint32_t bufferIndex, TransformSlotHandle slotHandle, const Core::Native::ObjectTransformGpu& transform) noexcept;
        Result commit(uint32_t bufferIndex) noexcept;

        Result get_upload_buffer(uint32_t bufferIndex, BufferHandle& outHandle) const;
        Result get_default_buffer(uint32_t bufferIndex, BufferHandle& outHandle) const;

        [[nodiscard]] uint32_t buffering_count() const noexcept
        {
            return m_bufferingCount;
        }

        [[nodiscard]] uint32_t slot_byte_size() const noexcept
        {
            return m_slotByteSize;
        }

        [[nodiscard]] uint64_t slot_byte_offset(TransformSlotHandle slotHandle) const noexcept
        {
            return static_cast<uint64_t>(slotHandle.slotIndex) * static_cast<uint64_t>(m_slotByteSize);
        }
    private:
        [[nodiscard]] Result validate_buffer_index(uint32_t bufferIndex) const noexcept;
    private:
        IBufferManager* m_bufferManager = nullptr;
        std::vector<BufferHandle> m_uploadBuffers{};
        std::vector<BufferHandle> m_defaultBuffers{};
        std::vector<SlotUploader<Core::Native::ObjectTransformGpu>> m_uploaders{};
        uint32_t m_bufferingCount = 0;
        uint32_t m_slotCapacity = 0;
        uint32_t m_slotByteSize = 0;
        uint32_t m_allocatedSlotCount = 0;
    };
} // namespace Cue::GraphicsCore
