#pragma once

// === RHI Includes ===
#include <TextureManager.h>

// === C++ includes ===
#include <vector>
#include <unordered_map>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "DescriptorAllocator.h"
#include "DX12RenderDevice.h"
#include "DX12GpuResource.h"

namespace Cue::RHI::DX12
{
    // 論理リソース
    struct DX12TextureRecord final
    {
        TextureDesc desc; // テクスチャの記述
        std::vector<DX12GpuResource> defaultResources; // デフォルトヒープテクスチャリソースの実体
        TableID descriptorTableId{};
    };

    class DX12TextureManager final : public ITextureManager
    {
    public:
        DX12TextureManager(DX12RenderDevice& renderDevice,
            DescriptorAllocator& descriptorAllocator)
            : m_renderDevice(renderDevice),
            m_descriptorAllocator(descriptorAllocator) {}
        ~DX12TextureManager() override = default;
        Result create_texture(const TextureDesc& desc, TextureHandle& out) override;
        Result create_texture(const TextureDesc& desc,
            std::span<const TextureSubresourceData> initialData,
            TextureHandle& out) override;
        Result destroy_texture(TextureHandle handle) override;
        Result get_texture_descriptor_index(TextureHandle handle,
            uint32_t& outIndex) override;
        Result get_texture(std::string_view name, TextureHandle& out) override;
        bool try_get_record(TextureHandle handle, DX12TextureRecord** outRecord);

        // 外部テクスチャを登録
        Result register_external_texture(DX12TextureRecord& record, TextureHandle& out);
    private:
        Result validate_texture_desc(const TextureDesc& desc) const;
        Result create_default_resource(const TextureDesc& desc,
            D3D12_RESOURCE_STATES initialState,
            DX12GpuResource& outResource) const;
        Result upload_initial_data(DX12GpuResource& resource,
            std::span<const TextureSubresourceData> initialData) const;
    private:
        DX12RenderDevice& m_renderDevice; // レンダーデバイスへの参照
        DescriptorAllocator& m_descriptorAllocator;
        Core::Registry<TextureTag, DX12TextureRecord> m_textureRegistry; // 論理テクスチャリソースのレジストリ
        std::unordered_map<Core::ResourceNameId, TextureHandle> m_nameToHandlesMap; // 名前からハンドルへのマッピング
    };
}
