#include "DX12BufferManager.h"

namespace Cue::GraphicsCore::DX12
{
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
    Result DX12BufferManager::get_buffer_instance_count(ResourceNameId nameId, uint32_t& outCount)
    {
        const auto it = m_bufferNameMap.find(nameId);
        if (it == m_bufferNameMap.end())
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Buffer not found");
        }

        outCount = static_cast<uint32_t>(it->second.size());
        return Result::ok();
    }
    Result DX12BufferManager::try_get_buffer(const BufferHandle& handle, GpuBufferResource*& outBuffer)
    {
        // 1) ハンドル解決に失敗した時点で null を返し、無効参照をその場で止める。
        outBuffer = nullptr;
        const bool found = m_bufferRegistry.with(handle, [&outBuffer](GpuBufferResource& buffer)
        {
            outBuffer = &buffer;
        });
        if (!found)
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Buffer handle is not alive");
        }
        if (outBuffer == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Buffer resource is not available");
        }

        return Result::ok();
    }
} // namespace Cue::GraphicsCore::DX12
