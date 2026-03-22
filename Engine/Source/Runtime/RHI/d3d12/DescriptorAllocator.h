#pragma once

// === RHI include ===
#include <RHICommon.h>

// === DirectX 12 include ===
#include "stdafx.h"
#include "DX12RenderDevice.h"

namespace Cue::RHI::DX12
{
    // デスクリプタヒープの種類
    enum class HeapType : uint8_t
    {
        CBV_SRV_UAV,
        SAMPLER,
        RTV,
        DSV,
        kCount
    };

    // ディスクリプタテーブルの種類
    enum class TableKind : uint8_t
    {
        Textures,
        Buffers,
        RenderTargets,
        DepthStencils
    };

    // テーブルID
    struct TableID final
    {
        TableKind m_kind = TableKind::Buffers;
        uint16_t  m_generation{};  ///< テーブルの世代（将来の再配置用）
        uint32_t  m_index = kInvalid;       ///< テーブル内のローカル index（0..capacity-1）

        static constexpr uint32_t kInvalid = 0xFFFFFFFF;

        bool valid() const { return m_index != kInvalid; }
    };

    class DescriptorAllocator final
    {
    public:
        DescriptorAllocator(DX12RenderDevice& device):
            m_device(device)
        {
        }
        // コピー禁止
        DescriptorAllocator(const DescriptorAllocator&) = delete;
        DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;
        // ムーブ禁止
        DescriptorAllocator(DescriptorAllocator&&) = delete;
        DescriptorAllocator& operator=(DescriptorAllocator&&) = delete;
        ~DescriptorAllocator() = default;

        // 初期化
        Result initialize(uint32_t texCap, uint32_t bufCap, uint32_t rtvCap, uint32_t dsvCap);
    private:
        DX12RenderDevice& m_device;
    };
}
