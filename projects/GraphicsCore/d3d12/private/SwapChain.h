#pragma once
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "DescriptorAllocator.h"
#include "DX12GpuCommand.h"
#include "DX12TextureManager.h"

namespace Cue::GraphicsCore::DX12
{
    class SwapChain final
    {
    public:
        SwapChain(DX12RenderDevice& renderDevice, DescriptorAllocator& descAllocator)
            : m_renderDevice(renderDevice), m_descriptorAllocator(descAllocator)
        {
        }
        ~SwapChain() = default;

        [[nodiscard]] Result create(
            HWND hWnd,
            uint32_t width,
            uint32_t height,
            uint32_t bufferCount,
            DX12QueueContext& queue,
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
        Result present(uint32_t syncInterval = 1, uint32_t flags = 0)
        {
            const HRESULT hr = m_swapChain->Present(syncInterval, flags);
            if (FAILED(hr))
            {
                return Result::fail(Facility::D3D12, Code::GettingInfoFailed, Severity::Error, static_cast<uint32_t>(hr), "Failed to present swap chain");
            }
            return Result::ok();
        }
        Result get_back_buffer(uint32_t index, GpuTextureResource& outBuffer) const
        {
            if (index < m_backBuffers.size())
            {
                outBuffer = m_backBuffers[index];
                return Result::ok();
            }
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Warning, 0, "Back buffer not found");
        }
        DescriptorAllocator::TableID get_rtv_table_id(uint32_t index) const
        {
            if (index < m_rtvTableIDs.size())
            {
                return m_rtvTableIDs[index];
            }
            return DescriptorAllocator::TableID{}; // 無効なテーブルIDを返す
        }
        Result import_back_buffers(DX12TextureManager& textureManager, std::vector<TextureHandle>& outHandles) const
        {
            outHandles.clear();
            outHandles.reserve(m_backBuffers.size());

            for (uint32_t index = 0; index < static_cast<uint32_t>(m_backBuffers.size()); ++index)
            {
                const std::string name = "SwapChain.BackBuffer." + std::to_string(index);
                const ResourceNameId nameId = fnv1a64(name);

                TextureHandle handle{};
                const Result result = textureManager.import_external_texture(nameId, m_backBuffers[index], handle);
                if (!result)
                {
                    return result;
                }
                outHandles.push_back(handle);
            }

            return Result::ok();
        }
    private:
        DX12RenderDevice& m_renderDevice;
        DescriptorAllocator& m_descriptorAllocator;

        HWND m_hWnd = nullptr;
        ComPtr<IDXGISwapChain4> m_swapChain;
        DXGI_SWAP_CHAIN_DESC1 m_desc{};
        int32_t m_refreshrate = 60;
        std::vector<DescriptorAllocator::TableID> m_rtvTableIDs;
        std::vector<GpuTextureResource> m_backBuffers;
    };
}
