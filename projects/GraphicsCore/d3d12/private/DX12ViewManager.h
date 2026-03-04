#pragma once
#include "stdafx.h"
#include "DescriptorAllocator.h"
#include "DX12BufferManager.h"
#include "DX12TextureManager.h"
#include <Registry.h>
#include <ViewManager.h>

namespace Cue::GraphicsCore::DX12
{
    class DX12ViewManager final : public IViewManager
    {
    public:
        DX12ViewManager(
            DX12BufferManager& bufferManager,
            DX12TextureManager& textureManager,
            DescriptorAllocator& descriptorAllocator) noexcept
            : m_bufferManager(bufferManager)
            , m_textureManager(textureManager)
            , m_descriptorAllocator(descriptorAllocator)
        {
        }
        ~DX12ViewManager() override = default;

        Result get_buffer_view(BufferHandle handle, const BufferViewDesc& desc, ViewHandle& outHandle) override;
        Result get_texture_view(TextureHandle handle, const TextureViewDesc& desc, ViewHandle& outHandle) override;
        Result destroy_view(ViewHandle handle) override;
        Result get_descriptor_handle(ViewHandle handle, DescriptorHandle& outHandle) const override;

    private:
        struct BufferViewKey final
        {
            BufferHandle handle{};
            BufferViewDesc desc{};

            [[nodiscard]] bool operator==(const BufferViewKey& other) const noexcept
            {
                return handle == other.handle && desc == other.desc;
            }
        };

        struct TextureViewKey final
        {
            TextureHandle handle{};
            TextureViewDesc desc{};

            [[nodiscard]] bool operator==(const TextureViewKey& other) const noexcept
            {
                return handle == other.handle && desc == other.desc;
            }
        };

        struct BufferViewKeyHash final
        {
            [[nodiscard]] size_t operator()(const BufferViewKey& key) const noexcept;
        };

        struct TextureViewKeyHash final
        {
            [[nodiscard]] size_t operator()(const TextureViewKey& key) const noexcept;
        };

        struct ViewRecord final
        {
            ResourceKind resourceKind = ResourceKind::Buffer;
            ViewType viewType = ViewType::ShaderResource;
            BufferHandle bufferHandle{};
            TextureHandle textureHandle{};
            BufferViewDesc bufferDesc{};
            TextureViewDesc textureDesc{};
            DescriptorAllocator::TableID tableId{};
        };

        [[nodiscard]] Result create_buffer_view(BufferHandle handle, const BufferViewDesc& desc, ViewHandle& outHandle);
        [[nodiscard]] Result create_texture_view(TextureHandle handle, const TextureViewDesc& desc, ViewHandle& outHandle);

    private:
        DX12BufferManager& m_bufferManager;
        DX12TextureManager& m_textureManager;
        DescriptorAllocator& m_descriptorAllocator;
        Registry<ViewTag, ViewRecord> m_viewRegistry;
        std::unordered_map<BufferViewKey, ViewHandle, BufferViewKeyHash> m_bufferViewCache;
        std::unordered_map<TextureViewKey, ViewHandle, TextureViewKeyHash> m_textureViewCache;
    };
} // namespace Cue::GraphicsCore::DX12
