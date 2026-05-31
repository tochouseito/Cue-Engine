#pragma once

/// ************************************************************************************
/// D3D12 ビューマネージャー
/// ************************************************************************************

// === RHI Includes ===
#include <ViewManager.h>

// === DirectX 12 includes ===
#include "DescriptorAllocator.h"
#include "DX12BufferManager.h"
#include "DX12TextureManager.h"

// === C++ includes ===
#include <unordered_map>

namespace Cue::RHI::DX12
{
    // ViewHandle から参照される descriptor table 群。
    // default/upload の両 resource に同じ ViewDesc から個別の descriptor を作るため配列で保持する。
    struct DX12ViewRecord final
    {
        ViewDesc desc{};
        std::vector<TableID> defaultTableIds; // デフォルトヒープビューのテーブルID
        std::vector<TableID> uploadTableIds; // アップロード用ビューのテーブルID
    };

    /// @brief Buffer/Texture に対する CBV/SRV/UAV/RTV/DSV 作成を集約する。
    /// @details ViewDesc を D3D12 descriptor に変換し、DescriptorAllocator の table lifetime と結び付ける。
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
        {}
        ~DX12ViewManager() override = default;
        Result create_view(const ViewDesc& desc, ViewHandle& out) override;
        Result destroy_view(ViewHandle handle) override;
        Result get_view(std::string_view name, ViewHandle& out) override;
        bool try_get_record(ViewHandle handle, DX12ViewRecord** outRecord);
    private:
        Result create_view_impl(const ViewDesc& desc, DX12GpuResource& resource, std::vector<TableID>& ids);
    private:
        DX12BufferManager& m_bufferManager;
        DX12TextureManager& m_textureManager;
        DescriptorAllocator& m_descriptorAllocator;
        Core::Registry<ViewTag, DX12ViewRecord> m_viewRegistry;
        std::unordered_map<Core::ResourceNameId, ViewHandle> m_nameToHandlesMap;
    };
}
