#pragma once

// === RHI Includes ===
#include <ViewManager.h>

// === C++ includes ===
#include <unordered_map>

// === DirectX 12 includes ===
#include "DescriptorAllocator.h"
#include "DX12BufferManager.h"
#include "DX12TextureManager.h"

namespace Cue::RHI::DX12
{
    struct DX12ViewRecord final
    {
        ViewDesc desc{};
        std::vector<TableID> defaultTableIds; // デフォルトヒープビューのテーブルID
        std::vector<TableID> uploadTableIds; // アップロード用ビューのテーブルID
    };

    class DX12ViewManager final : public IViewManager
    {
    public:
        DX12ViewManager(
            DX12BufferManager& bufferManager,
            DX12TextureManager& textureManager,
            DescriptorAllocator& descriptorAllocator) :
            m_bufferManager(bufferManager),
            m_textureManager(textureManager),
            m_descriptorAllocator(descriptorAllocator)
        {
        }
        ~DX12ViewManager() override = default;
        Result create_view(const ViewDesc& desc, ViewHandle& out) override;
        Result destroy_view(ViewHandle handle) override;
        Result clear_render_target(
            ICommandContext& commandContext,
            ViewHandle handle,
            uint32_t frameIndex,
            const std::array<float, 4>& clearColor) override;
        bool try_get_record(ViewHandle handle, const DX12ViewRecord*& outRecord) const;
        bool try_get_default_table(ViewHandle handle, uint32_t frameIndex, TableID& outTable) const;
        bool try_get_upload_table(ViewHandle handle, uint32_t frameIndex, TableID& outTable) const;
        DescriptorAllocator& descriptor_allocator() noexcept { return m_descriptorAllocator; }
    private:
        DX12BufferManager& m_bufferManager;
        DX12TextureManager& m_textureManager;
        DescriptorAllocator& m_descriptorAllocator;
        Registry<ViewTag, DX12ViewRecord> m_viewRegistry;
        std::unordered_map<Core::ResourceNameId, ViewHandle> m_nameToHandlesMap;
    };
}
