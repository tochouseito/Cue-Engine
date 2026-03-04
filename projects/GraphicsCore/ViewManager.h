#pragma once
#include "GraphicsCommon.h"
#include "ResourceHandle.h"

namespace Cue::GraphicsCore
{
    enum class ViewType : uint8_t
    {
        ConstantBuffer,
        ShaderResource,
        UnorderedAccess,
        RenderTarget,
        DepthStencil
    };

    struct BufferViewDesc final
    {
        ViewType type = ViewType::ShaderResource;
        uint64_t firstElement = 0;
        uint32_t numElements = 0;
        uint32_t structureByteStride = 0;
        uint64_t byteOffset = 0;
        uint32_t byteSize = 0;
        bool isRaw = false;

        [[nodiscard]] bool operator==(const BufferViewDesc& other) const noexcept
        {
            return type == other.type
                && firstElement == other.firstElement
                && numElements == other.numElements
                && structureByteStride == other.structureByteStride
                && byteOffset == other.byteOffset
                && byteSize == other.byteSize
                && isRaw == other.isRaw;
        }
    };

    struct TextureViewDesc final
    {
        ViewType type = ViewType::ShaderResource;
        uint32_t mipSlice = 0;
        uint32_t mipLevels = 1;

        [[nodiscard]] bool operator==(const TextureViewDesc& other) const noexcept
        {
            return type == other.type
                && mipSlice == other.mipSlice
                && mipLevels == other.mipLevels;
        }
    };

    struct DescriptorHandle final
    {
        uint64_t cpuPtr = 0;
        uint64_t gpuPtr = 0;
        bool shaderVisible = false;

        [[nodiscard]] bool valid() const noexcept
        {
            return cpuPtr != 0 || gpuPtr != 0;
        }
    };

    class IViewManager
    {
    public:
        IViewManager() = default;
        virtual ~IViewManager() = default;

        virtual Result get_buffer_view(BufferHandle handle, const BufferViewDesc& desc, ViewHandle& outHandle) = 0;
        virtual Result get_texture_view(TextureHandle handle, const TextureViewDesc& desc, ViewHandle& outHandle) = 0;
        virtual Result destroy_view(ViewHandle handle) = 0;
        virtual Result get_descriptor_handle(ViewHandle handle, DescriptorHandle& outHandle) const = 0;
    };
} // namespace Cue::GraphicsCore
