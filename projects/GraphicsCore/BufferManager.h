#pragma once
#include "GraphicsCommon.h"
#include "ResourceHandle.h"
#include "Registry.h"

namespace Cue::GraphicsCore
{
    struct BufferDesc
    {
        std::string_view name;
        BufferType type = BufferType::Unknown;
        ResourceHeapType heapType = ResourceHeapType::Default;
        ResourceState initialState = ResourceState::Common;
        uint32_t bufferingCount = 0;
        ResourceInstanceSource instanceSource = ResourceInstanceSource::FrameResourceIndex;
        uint32_t stride = 0; // StructuredBufferの要素サイズなど、リソースのインスタンスごとのサイズ
        uint32_t elementCount = 0; // StructuredBufferの要素数など、リソースのインスタンスごとの要素数
        uint32_t size = 0; // バッファ全体のサイズ（stride * インスタンス数など）
        uint32_t alignment = 0; // バッファのアライメント要件
    };

    class IBufferManager
    {
    public:
        IBufferManager() = default;
        virtual ~IBufferManager() = default;
        virtual Result create_buffer(const BufferDesc& desc, BufferHandle& outHandle) = 0;
        virtual Result destroy_buffer(const BufferHandle& handle) = 0;
        virtual Result get_buffer(ResourceNameId nameId, uint32_t bufferIndex, BufferHandle& outHandle) = 0;
        virtual Result get_buffer_instance_count(ResourceNameId nameId, uint32_t& outCount) = 0;
    };
} // namespace Cue::GraphicsCore
