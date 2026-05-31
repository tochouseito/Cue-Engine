#pragma once

/// ************************************************************************************
/// D3D12 バッファマネージャー
/// ************************************************************************************

// === RHI Includes ===
#include <BufferManager.h>

// === C++ includes ===
#include <vector>
#include <unordered_map>

// === DirectX 12 includes ===
#include "DX12Common.h"
#include "DX12RenderDevice.h"
#include "DX12GpuResource.h"

namespace Cue::RHI::DX12
{
    // 論理リソース
    // 1 つの BufferHandle に対して、用途別 heap の実体をまとめて保持する。
    // bufferCount 分の配列を持つことでフレームリングごとの upload/readback 領域を分離できる。
    struct DX12BufferRecord final
    {
        BufferDesc desc; // バッファの記述
        std::vector<DX12GpuResource> defaultResources; // デフォルトヒープバッファリソースの実体
        std::vector<DX12GpuResource> uploadResources; // アップロード用バッファリソースの実体
        std::vector<DX12GpuResource> readbackResources; // 読み戻し用バッファリソースの実体
    };

    /// @brief RHI BufferHandle と D3D12 buffer resource の対応を管理する。
    /// @details 名前引き、世代付き handle 検証、upload/readback view の解決を担当する。
    class DX12BufferManager final : public IBufferManager
    {
    public:
        DX12BufferManager(DX12RenderDevice& renderDevice) : m_renderDevice(renderDevice) {}
        ~DX12BufferManager() override = default;
        Result create_buffer(const BufferDesc& desc, BufferHandle& out) override;
        Result destroy_buffer(BufferHandle handle) override;
        Result get_buffer(std::string_view name, BufferHandle& out) override;
        Result get_upload_buffer_view(BufferHandle handle, UploadBufferView& outView) override;
        Result get_readback_buffer_view(BufferHandle handle, ReadbackBufferView& outView) override;
        bool try_get_record(BufferHandle handle, DX12BufferRecord** outRecord);
    private:
        DX12RenderDevice& m_renderDevice; // レンダーデバイスへの参照
        Core::Registry<BufferTag, DX12BufferRecord> m_bufferRegistry; // 論理バッファリソースのレジストリ
        std::unordered_map<Core::ResourceNameId, BufferHandle> m_nameToHandlesMap; // 名前からハンドルへのマッピング
    };
}
