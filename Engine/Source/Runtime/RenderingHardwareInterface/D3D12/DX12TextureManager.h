#pragma once

/// ************************************************************************************
/// D3D12 テクスチャマネージャー
/// ************************************************************************************

// === RHI Includes ===
#include <TextureManager.h>

// === Core includes ===
#include <IO/IFileSystem.h>

// === DirectX 12 includes ===
#include "DX12Common.h"
#include "DX12GpuResource.h"
#include "DX12RenderDevice.h"
#include "DescriptorAllocator.h"

// === C++ includes ===
#include <unordered_map>
#include <vector>

namespace Cue::RHI::DX12
{
    // 論理リソース
    // TextureHandle から参照される実体。SwapChain の back buffer など外部
    // resource も同じ形式で登録する。
    struct DX12TextureRecord final
    {
        TextureDesc desc; // テクスチャの記述
        std::vector<DX12GpuResource>
            defaultResources; // デフォルトヒープテクスチャリソースの実体
        TableID descriptorTableId{};
    };

    /// @brief RHI TextureHandle と D3D12 texture resource の対応を管理する。
    /// @details テクスチャ生成、初期データ upload、shader visible descriptor
    /// の確保を担当する。
    class DX12TextureManager final : public ITextureManager
    {
      public:
        DX12TextureManager(DX12RenderDevice& renderDevice,
                           DescriptorAllocator& descriptorAllocator,
                           Core::IO::IFileSystem& fileSystem)
            : m_renderDevice(renderDevice),
              m_descriptorAllocator(descriptorAllocator),
              m_fileSystem(fileSystem)
        {
        }
        ~DX12TextureManager() override = default;
        Result create_texture(const TextureDesc& desc,
                              TextureHandle& out) override;
        Result create_texture(
            const TextureDesc& desc,
            std::span<const TextureSubresourceData> initialData,
            TextureHandle& out) override;
        Result create_texture_from_file(std::string_view name,
                                        std::string_view filePath,
                                        TextureHandle& out) override;
        Result destroy_texture(TextureHandle handle) override;
        Result get_texture_descriptor_index(TextureHandle handle,
                                            uint32_t& outIndex) override;
        Result get_texture_desc(TextureHandle handle,
                                TextureDesc& outDesc) override;
        Result get_texture(std::string_view name, TextureHandle& out) override;
        bool try_get_record(TextureHandle handle,
                            DX12TextureRecord** outRecord);

        // 外部テクスチャを登録
        // SwapChain から取得した back buffer のように、この manager
        // が生成していない resource を管理下へ入れる。
        Result register_external_texture(DX12TextureRecord& record,
                                         TextureHandle& out);

      private:
        Result validate_texture_desc(const TextureDesc& desc) const;
        Result create_default_resource(const TextureDesc& desc,
                                       D3D12_RESOURCE_STATES initialState,
                                       DX12GpuResource& outResource) const;
        Result upload_initial_data(
            DX12GpuResource& resource,
            std::span<const TextureSubresourceData> initialData) const;
        Result upload_initial_data(
            DX12GpuResource& resource,
            std::span<const D3D12_SUBRESOURCE_DATA> initialData) const;

      private:
        DX12RenderDevice& m_renderDevice; // レンダーデバイスへの参照
        DescriptorAllocator& m_descriptorAllocator;
        Core::IO::IFileSystem& m_fileSystem;
        Core::Registry<TextureTag, DX12TextureRecord>
            m_textureRegistry; // 論理テクスチャリソースのレジストリ
        std::unordered_map<Core::ResourceNameId, TextureHandle>
            m_nameToHandlesMap; // 名前からハンドルへのマッピング
    };
} // namespace Cue::RHI::DX12
