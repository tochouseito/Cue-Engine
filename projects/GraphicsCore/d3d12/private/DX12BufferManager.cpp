#include "DX12BufferManager.h"

namespace Cue::GraphicsCore::DX12
{
    namespace
    {
        [[nodiscard]] bool is_supported_buffer_type(BufferType type) noexcept
        {
            switch (type)
            {
            case BufferType::Vertex:
            case BufferType::Index:
            case BufferType::Constant:
            case BufferType::Structured:
            case BufferType::UnorderedAccess:
            case BufferType::Raw:
                return true;
            default:
                return false;
            }
        }

        D3D12_HEAP_TYPE convert_heap_type(ResourceHeapType heapType)
        {
            switch (heapType)
            {
            case ResourceHeapType::Default:
                return D3D12_HEAP_TYPE_DEFAULT;
            case ResourceHeapType::Upload:
                return D3D12_HEAP_TYPE_UPLOAD;
            default:
                return D3D12_HEAP_TYPE_DEFAULT;
            }
        }

        D3D12_RESOURCE_STATES convert_resource_state(ResourceState state)
        {
            switch (state)
            {
            case ResourceState::Common:
                return D3D12_RESOURCE_STATE_COMMON;
            case ResourceState::CopySource:
                return D3D12_RESOURCE_STATE_COPY_SOURCE;
            case ResourceState::CopyDest:
                return D3D12_RESOURCE_STATE_COPY_DEST;
            case ResourceState::RenderTarget:
                return D3D12_RESOURCE_STATE_RENDER_TARGET;
            case ResourceState::UnorderedAccess:
                return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            case ResourceState::ShaderResource:
                return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            case ResourceState::DepthWrite:
                return D3D12_RESOURCE_STATE_DEPTH_WRITE;
            case ResourceState::Present:
                return D3D12_RESOURCE_STATE_PRESENT;
            default:
                return D3D12_RESOURCE_STATE_COMMON;
            }
        }
    }

    Result DX12BufferManager::create_buffer(const BufferDesc& desc, BufferHandle& outHandle)
    {
        const uint32_t bufferingCount = (std::max)(desc.bufferingCount, 1u);

        std::vector<BufferHandle> handles;
        handles.reserve(bufferingCount);

        for (uint32_t bufferIndex = 0; bufferIndex < bufferingCount; ++bufferIndex)
        {
            GpuBufferResource buffer{};
            BufferCreateDesc createDesc{};
            std::wstring resourceName = to_utf16(desc.name);
            if (!is_supported_buffer_type(desc.type))
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Unsupported buffer type");
            }

            // 1) バッファ種別ごとの差は descriptor 作成時に吸収し、実体生成は共通経路へ寄せる。
            createDesc.name = resourceName;
            createDesc.heapType = convert_heap_type(desc.heapType);
            createDesc.initialState = convert_resource_state(desc.initialState);
            createDesc.flags = D3D12_RESOURCE_FLAG_NONE;
            createDesc.byteSize = desc.size;
            createDesc.numElements = desc.elementCount;
            createDesc.stride = desc.stride;
            const Result createResult = buffer.create_buffer(*m_renderDevice.get_d3d12_device(), createDesc);
            if (!createResult)
            {
                return createResult;
            }

            BufferHandle handle = m_bufferRegistry.create(buffer);
            handles.push_back(handle);
        }

        outHandle = handles.front();
        if (!desc.name.empty())
        {
            m_bufferNameMap[Core::fnv1a64(desc.name)] = std::move(handles);
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
    Result DX12BufferManager::write_buffer(const BufferHandle& handle, uint64_t byteOffset, const void* data, uint32_t byteSize)
    {
        // 1) UploadBuffer 以外への CPU 書き込みは backend 依存の誤用なので、その場で止める。
        if (data == nullptr || byteSize == 0)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Buffer write source is invalid");
        }

        GpuBufferResource* buffer = nullptr;
        const Result resolveResult = try_get_buffer(handle, buffer);
        if (!resolveResult)
        {
            return resolveResult;
        }

        ID3D12Resource* resource = buffer->get_resource();
        if (resource == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Buffer resource is null");
        }
        if ((byteOffset + byteSize) > buffer->get_byte_size())
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Buffer write range exceeds resource size");
        }

        D3D12_HEAP_PROPERTIES actualHeapProperties{};
        D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
        HRESULT hr = resource->GetHeapProperties(&actualHeapProperties, &heapFlags);
        if (FAILED(hr))
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, static_cast<uint32_t>(hr), "Failed to query heap properties");
        }
        if (actualHeapProperties.Type != D3D12_HEAP_TYPE_UPLOAD)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Buffer is not created on upload heap");
        }

        void* mappedData = nullptr;
        D3D12_RANGE readRange{ 0, 0 };
        hr = resource->Map(0, &readRange, &mappedData);
        if (FAILED(hr) || mappedData == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, static_cast<uint32_t>(hr), "Failed to map upload buffer");
        }

        std::memcpy(static_cast<std::byte*>(mappedData) + byteOffset, data, byteSize);

        D3D12_RANGE writtenRange{};
        writtenRange.Begin = static_cast<SIZE_T>(byteOffset);
        writtenRange.End = static_cast<SIZE_T>(byteOffset + byteSize);
        resource->Unmap(0, &writtenRange);
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
