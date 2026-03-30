#pragma once

// === DirectX 12 includes ===
#include "stdafx.h"

namespace Cue::RHI::DX12
{
    class DX12GpuResource : public GpuResource
    {
    public:
        DX12GpuResource() = default;
        DX12GpuResource(const DX12GpuResource&) = delete;
        DX12GpuResource& operator=(const DX12GpuResource&) = delete;
        DX12GpuResource(DX12GpuResource&&) noexcept = default;
        DX12GpuResource& operator=(DX12GpuResource&&) noexcept = default;
        ~DX12GpuResource() override = default;

        operator bool() const { return m_resource != nullptr; }

        // 破棄
        bool destroy()
        {
            if (is_in_use())
            {
                return false;
            }
            if (m_resource)
            {
                unmap_if_needed();
                m_resource.Reset();
            }
            m_currentState = D3D12_RESOURCE_STATE_COMMON;
            m_resourceDesc = {};
            m_fence = nullptr;
            m_fenceValue = 0;
            m_mappedData = nullptr;
            return true;
        }

        Result create(
            ID3D12Device& device,
            const D3D12_HEAP_PROPERTIES& heapProperties,
            D3D12_HEAP_FLAGS heapFlags,
            const D3D12_RESOURCE_DESC& desc,
            D3D12_RESOURCE_STATES initialState,
            const D3D12_CLEAR_VALUE* clearValue,
            std::wstring_view name)
        {
            // リソースの作成
            HRESULT hr = device.CreateCommittedResource(
                &heapProperties,
                heapFlags,
                &desc,
                initialState,
                clearValue,
                IID_PPV_ARGS(&m_resource));
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error, "Failed to create D3D12 resource.");
            }
            // リソース名の設定
            if (name.size() > 0)
            {
                std::wstring nameStr(name);
                m_resource->SetName(nameStr.c_str());
            }
            // メンバ変数の設定
            m_currentState = initialState;
            m_resourceDesc = desc;
            m_bufferSize = static_cast<uint64_t>(desc.Width);
            return Result::ok();
        }

        Result map_persistent()
        {
            // 1) 未生成リソースへの Map を防ぎ、呼び出し順の破綻を早期に止める。
            if (!m_resource)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Cannot map a null D3D12 resource.");
            }

            // 2) 既に Map 済みなら同じ CPU ポインタを再利用して多重 Map を避ける。
            if (m_mappedData != nullptr)
            {
                return Result::ok();
            }

            // 3) CPU 書き込み専用として永続 Map し、以後の uploader 初期化に使う。
            D3D12_RANGE readRange{};
            void* mappedData = nullptr;
            const HRESULT hr = m_resource->Map(0, &readRange, &mappedData);
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr),
                    Severity::Error,
                    "Failed to map D3D12 resource.");
            }

            m_mappedData = static_cast<std::byte*>(mappedData);
            return Result::ok();
        }

        void unmap_if_needed() noexcept
        {
            // 1) 永続 Map を破棄前に閉じて、外部に残った CPU ポインタを無効化する。
            if (!m_resource || m_mappedData == nullptr)
            {
                return;
            }

            // 2) 書き込み専用運用なので書き戻し範囲は nullptr で十分。
            m_resource->Unmap(0, nullptr);
            m_mappedData = nullptr;
        }

        // リソースが使用中かどうか
        bool is_in_use() const
        {
            // フェンスが設定されていれば、完了値と比較して使用中かどうかを判断
            if (m_fence)
            {
                return m_fence->GetCompletedValue() < m_fenceValue;
            }
            return false;
        }

        ID3D12Resource* get_resource() const noexcept
        {
            return m_resource.Get();
        }

        D3D12_GPU_VIRTUAL_ADDRESS get_gpu_virtual_address() const noexcept
        {
            if (!m_resource)
            {
                return D3D12_GPU_VIRTUAL_ADDRESS_NULL;
            }
            return m_resource->GetGPUVirtualAddress();
        }

        const D3D12_RESOURCE_DESC& get_resource_desc() const noexcept
        {
            return m_resourceDesc;
        }

        uint64_t get_buffer_size() const noexcept
        {
            return m_bufferSize;
        }

        ResourceState current_state() const noexcept override
        {
            return convert_resource_state(m_currentState);
        }

        D3D12_RESOURCE_STATES get_current_d3d12_state() const noexcept
        {
            return m_currentState;
        }

        void set_current_state(D3D12_RESOURCE_STATES a_state) noexcept
        {
            m_currentState = a_state;
        }

        std::byte* mapped_data() const noexcept
        {
            return m_mappedData;
        }
    private:
        ComPtr<ID3D12Resource> m_resource = nullptr;
        D3D12_RESOURCE_STATES m_currentState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_DESC m_resourceDesc{};
        ComPtr<ID3D12Fence> m_fence = nullptr;
        uint64_t m_fenceValue = 0;
        uint64_t m_bufferSize = 0;
        std::byte* m_mappedData = nullptr;
    };
}
