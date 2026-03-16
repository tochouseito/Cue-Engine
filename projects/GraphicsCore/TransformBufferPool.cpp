#include "TransformBufferPool.h"

namespace Cue::GraphicsCore
{
    namespace
    {
        [[nodiscard]] constexpr uint32_t round_up_to_multiple(uint32_t value, uint32_t alignment) noexcept
        {
            return alignment == 0 ? value : ((value + alignment - 1u) / alignment) * alignment;
        }

        [[nodiscard]] std::string make_upload_buffer_name(uint32_t bufferIndex)
        {
            return "TransformPool.Upload." + std::to_string(bufferIndex);
        }

        [[nodiscard]] std::string make_default_buffer_name(uint32_t bufferIndex)
        {
            return "TransformPool.Default." + std::to_string(bufferIndex);
        }
    }

    Result TransformBufferPool::initialize(const TransformBufferPoolDesc& desc, IBufferManager& bufferManager)
    {
        // 1) slot 数と frame 数を確定し、update/render のリング運用に必要な実体数を固定する。
        if (desc.slotCapacity == 0)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "TransformBufferPool slot capacity must be greater than zero.");
        }

        m_bufferManager = &bufferManager;
        m_bufferingCount = (desc.bufferingCount == 0) ? 1u : desc.bufferingCount;
        m_slotCapacity = desc.slotCapacity;
        m_slotByteSize = round_up_to_multiple(
            static_cast<uint32_t>(sizeof(Core::Native::ObjectTransformGpu)),
            256u);
        m_allocatedSlotCount = 0;
        m_uploadBuffers.clear();
        m_defaultBuffers.clear();
        m_uploaders.clear();
        m_uploadBuffers.reserve(m_bufferingCount);
        m_defaultBuffers.reserve(m_bufferingCount);
        m_uploaders.resize(m_bufferingCount);

        const uint32_t totalBufferByteSize = m_slotByteSize * m_slotCapacity;

        // 2) 各 frame index に upload/default の実体を持たせ、update thread と render thread の競合を避ける。
        for (uint32_t bufferIndex = 0; bufferIndex < m_bufferingCount; ++bufferIndex)
        {
            const std::string uploadBufferName = make_upload_buffer_name(bufferIndex);
            BufferDesc uploadDesc{};
            uploadDesc.name = uploadBufferName;
            uploadDesc.type = BufferType::Constant;
            uploadDesc.heapType = ResourceHeapType::Upload;
            uploadDesc.initialState = ResourceState::CopySource;
            uploadDesc.size = totalBufferByteSize;

            BufferHandle uploadBuffer{};
            Result result = m_bufferManager->create_buffer(uploadDesc, uploadBuffer);
            if (!result)
            {
                shutdown();
                return result;
            }
            m_uploadBuffers.push_back(uploadBuffer);

            const std::string defaultBufferName = make_default_buffer_name(bufferIndex);
            BufferDesc defaultDesc{};
            defaultDesc.name = defaultBufferName;
            defaultDesc.type = BufferType::Constant;
            defaultDesc.heapType = ResourceHeapType::Default;
            defaultDesc.initialState = ResourceState::Common;
            defaultDesc.size = totalBufferByteSize;

            BufferHandle defaultBuffer{};
            result = m_bufferManager->create_buffer(defaultDesc, defaultBuffer);
            if (!result)
            {
                shutdown();
                return result;
            }
            m_defaultBuffers.push_back(defaultBuffer);

            std::byte* mappedData = nullptr;
            result = m_bufferManager->map_upload_buffer(uploadBuffer, mappedData);
            if (!result)
            {
                shutdown();
                return result;
            }

            // 3) SlotUploader へ永続 map ポインタを渡し、update 側の仕事を slot 単位 memcpy だけに限定する。
            m_uploaders[bufferIndex].initialize(m_slotCapacity, 256u, mappedData);
        }

        return Result::ok();
    }

    Result TransformBufferPool::shutdown()
    {
        // 1) pool 所有の GPU バッファを全解放し、backend shutdown 後に dangling handle を残さないようにする。
        if (m_bufferManager != nullptr)
        {
            for (const BufferHandle& handle : m_defaultBuffers)
            {
                if (handle.valid())
                {
                    (void)m_bufferManager->destroy_buffer(handle);
                }
            }
            for (const BufferHandle& handle : m_uploadBuffers)
            {
                if (handle.valid())
                {
                    (void)m_bufferManager->destroy_buffer(handle);
                }
            }
        }

        m_uploadBuffers.clear();
        m_defaultBuffers.clear();
        m_uploaders.clear();
        m_bufferManager = nullptr;
        m_bufferingCount = 0;
        m_slotCapacity = 0;
        m_slotByteSize = 0;
        m_allocatedSlotCount = 0;
        return Result::ok();
    }

    Result TransformBufferPool::allocate_slot(TransformSlotHandle& outHandle)
    {
        // 1) slot 上限を超えた割り当てを止め、複数オブジェクト追加時のバッファ破壊を防ぐ。
        outHandle = {};
        if (m_bufferManager == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "TransformBufferPool is not initialized.");
        }
        if (m_allocatedSlotCount >= m_slotCapacity)
        {
            return Result::fail(Facility::Graphics, Code::OutOfMemory, Severity::Error, 0, "TransformBufferPool has no free slot.");
        }

        outHandle.slotIndex = m_allocatedSlotCount;
        ++m_allocatedSlotCount;
        return Result::ok();
    }

    Result TransformBufferPool::begin_frame(uint32_t bufferIndex)
    {
        // 1) 対象 frame index の upload キューだけをクリアし、別 index の commit 済みデータを保持する。
        const Result validateResult = validate_buffer_index(bufferIndex);
        if (!validateResult)
        {
            return validateResult;
        }

        m_uploaders[bufferIndex].begin_frame();
        return Result::ok();
    }

    Result TransformBufferPool::push(uint32_t bufferIndex, TransformSlotHandle slotHandle, const Core::Native::ObjectTransformGpu& transform) noexcept
    {
        // 1) 無効 slot/index は拒否し、SlotUploader へ壊れた範囲情報を渡さない。
        const Result validateResult = validate_buffer_index(bufferIndex);
        if (!validateResult)
        {
            return validateResult;
        }
        if (!slotHandle.valid())
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Transform slot handle is invalid.");
        }

        // 2) SlotUploader の最後勝ちと連続区間 memcpy をそのまま再利用する。
        if (!m_uploaders[bufferIndex].push(slotHandle.slotIndex, transform))
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Transform slot push failed.");
        }

        return Result::ok();
    }

    Result TransformBufferPool::commit(uint32_t bufferIndex) noexcept
    {
        // 1) 更新済み slot 群を upload buffer へ反映し、render pass では copy source として読むだけにする。
        const Result validateResult = validate_buffer_index(bufferIndex);
        if (!validateResult)
        {
            return validateResult;
        }

        if (!m_uploaders[bufferIndex].commit())
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Transform uploader commit failed.");
        }

        return Result::ok();
    }

    Result TransformBufferPool::get_upload_buffer(uint32_t bufferIndex, BufferHandle& outHandle) const
    {
        // 1) render pass が copy source に使う upload handle を返す。
        const Result validateResult = validate_buffer_index(bufferIndex);
        if (!validateResult)
        {
            return validateResult;
        }

        outHandle = m_uploadBuffers[bufferIndex];
        return Result::ok();
    }

    Result TransformBufferPool::get_default_buffer(uint32_t bufferIndex, BufferHandle& outHandle) const
    {
        // 1) render pass が CBV bind に使う default handle を返す。
        const Result validateResult = validate_buffer_index(bufferIndex);
        if (!validateResult)
        {
            return validateResult;
        }

        outHandle = m_defaultBuffers[bufferIndex];
        return Result::ok();
    }

    Result TransformBufferPool::validate_buffer_index(uint32_t bufferIndex) const noexcept
    {
        // 1) ring buffer 範囲外のアクセスを拒否し、update/render の別 index 運用を安全に保つ。
        if (m_bufferManager == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "TransformBufferPool is not initialized.");
        }
        if (bufferIndex >= m_bufferingCount)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "TransformBufferPool buffer index is out of range.");
        }

        return Result::ok();
    }
} // namespace Cue::GraphicsCore
