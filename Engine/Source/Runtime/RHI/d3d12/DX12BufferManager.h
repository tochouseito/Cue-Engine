#pragma once

// === RHI Includes ===
#include <BufferManager.h>

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
    struct DX12BufferRecord final
    {
        BufferDesc desc; // バッファの記述
        std::vector<DX12GpuResource> defaultResources; // デフォルトヒープバッファリソースの実体
        std::vector<DX12GpuResource> uploadResources; // アップロード用バッファリソースの実体
    };

    class DX12BufferManager final : public IBufferManager
    {
    public:
        DX12BufferManager(DX12RenderDevice& renderDevice) : m_renderDevice(renderDevice) {}
        ~DX12BufferManager() override = default;
        Result create_buffer(const BufferDesc& desc, BufferHandle& out) override;
        Result destroy_buffer(BufferHandle handle) override;
        Result get_buffer(std::string_view name, BufferHandle& out) override;
        Result get_upload_buffer_view(BufferHandle handle, UploadBufferView& outView) override;
        bool try_get_record(BufferHandle handle, DX12BufferRecord** outRecord);
    private:
        DX12RenderDevice& m_renderDevice; // レンダーデバイスへの参照
        Core::Registry<BufferTag, DX12BufferRecord> m_bufferRegistry; // 論理バッファリソースのレジストリ
        std::unordered_map<Core::ResourceNameId, BufferHandle> m_nameToHandlesMap; // 名前からハンドルへのマッピング
    };
}
