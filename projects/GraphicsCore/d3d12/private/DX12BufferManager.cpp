#include "DX12BufferManager.h"

namespace Cue::GraphicsCore::DX12
{
    DX12BufferManager::DX12BufferManager(DX12RenderDevice& renderDevice)
    {
    }
    Result DX12BufferManager::create_buffer(const BufferDesc& desc, BufferHandle& outHandle)
    {
        const uint32_t bufferingCount = (std::max)(desc.bufferingCount, 1u);

        std::vector<BufferHandle> handles;
        handles.reserve(bufferingCount);

        for (uint32_t bufferIndex = 0; bufferIndex < bufferingCount; ++bufferIndex)
        {
            GpuBufferResource buffer{};
            BufferHandle handle = m_bufferRegistry.create(buffer);
            handles.push_back(handle);
        }

        outHandle = handles.front();
        if (!desc.name.empty())
        {
            m_bufferNameMap[fnv1a64(desc.name)] = std::move(handles);
        }

        return Result::ok();
    }
    Result DX12BufferManager::destroy_buffer(const BufferHandle& handle)
    {
        if (!m_bufferRegistry.destroy(handle))
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Buffer handle is not alive");
        }

        std::erase_if(m_bufferNameMap, [&handle](auto& pair)
            {
                auto& handles = pair.second;
                std::erase(handles, handle);
                return handles.empty();
            });

        return Result::ok();
    }
    Result DX12BufferManager::get_buffer(ResourceNameId nameId, uint32_t bufferIndex, BufferHandle& outHandle)
    {
        const auto it = m_bufferNameMap.find(nameId);
        if (it == m_bufferNameMap.end())
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Buffer not found");
        }

        const auto& handles = it->second;
        if (bufferIndex >= handles.size())
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Buffered buffer not found");
        }

        outHandle = handles[bufferIndex];
        return Result::ok();
    }
} // namespace Cue::GraphicsCore::DX12
