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
                m_resource.Reset();
            }
            m_currentState = D3D12_RESOURCE_STATE_COMMON;
            m_resourceDesc = {};
            m_fence = nullptr;
            m_fenceValue = 0;
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
    private:
        ComPtr<ID3D12Resource> m_resource = nullptr;
        D3D12_RESOURCE_STATES m_currentState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_DESC m_resourceDesc{};
        ComPtr<ID3D12Fence> m_fence = nullptr;
        uint64_t m_fenceValue = 0;
        uint64_t m_bufferSize = 0;
    };
}
