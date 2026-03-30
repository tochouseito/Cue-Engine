#pragma once

// === RHI Includes ===
#include <TextureManager.h>

// === C++ includes ===
#include <vector>
#include <unordered_map>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "DX12GpuResource.h"

namespace Cue::RHI::DX12
{
    // 論理リソース
    struct DX12TextureRecord final
    {
        TextureDesc desc; // テクスチャの記述
        std::vector<DX12GpuResource> defaultResources; // デフォルトヒープテクスチャリソースの実体
    };

    class DX12TextureManager final : public ITextureManager
    {
    public:
        DX12TextureManager(DX12RenderDevice& renderDevice) : m_renderDevice(renderDevice) {}
        ~DX12TextureManager() override = default;
        Result create_texture(const TextureDesc& desc, TextureHandle& out) override;
        Result destroy_texture(TextureHandle handle) override;
        bool try_get_record(TextureHandle handle, DX12TextureRecord** outRecord);
    private:
        DX12RenderDevice& m_renderDevice; // レンダーデバイスへの参照
        Core::Registry<TextureTag, DX12TextureRecord> m_textureRegistry; // 論理テクスチャリソースのレジストリ
        std::unordered_map<Core::ResourceNameId, TextureHandle> m_nameToHandlesMap; // 名前からハンドルへのマッピング
    };
}
