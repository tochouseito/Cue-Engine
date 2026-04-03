#pragma once

// === RHI includes ===
#include <RHICommon.h>

// === C++ includes ===
#include <mutex>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "DescriptorAllocator.h"
#include "DX12BufferManager.h"
#include "DX12TextureManager.h"
#include "DX12ViewManager.h"
#include "DX12PipelineManager.h"

namespace Cue::RHI::DX12
{
    class DX12GpuCommandContext final : public ICommandContext
    {
    public:
        DX12GpuCommandContext(ID3D12Device& device,
            DescriptorAllocator& descriptorAllocator,
            DX12BufferManager& bufferManager,
            DX12TextureManager& textureManager,
            DX12ViewManager& viewManager,
            DX12PipelineManager& pipelineManager,
            D3D12_COMMAND_LIST_TYPE type);
        // コピー禁止
        DX12GpuCommandContext(const DX12GpuCommandContext&) = delete;
        DX12GpuCommandContext& operator=(const DX12GpuCommandContext&) = delete;
        // ムーブは許可
        DX12GpuCommandContext(DX12GpuCommandContext&&) = default;
        DX12GpuCommandContext& operator=(DX12GpuCommandContext&&) = default;
        ~DX12GpuCommandContext() override = default;

        Result setup(uint32_t frameIndex, uint32_t bufferCount) override;
        Result reset() override;
        Result close() override;
        CommandListType type() const override;
        void* native_command_list() const override { return m_commandList.Get(); }

        // --- 取得 ---
        ID3D12GraphicsCommandList* d3d12_command_list() const { return m_commandList.Get(); }
        ID3D12CommandAllocator* d3d12_command_allocator() const { return m_commandAllocator.Get(); }

        /// --- GPU プロファイリング用のイベントマーカー ---
        void begin_event(const char* name) override;
        void end_event() override;

        // --- Commands ---
        Result resource_barrier(BufferHandle handle, const ResourceBarrierDesc desc) override;
        Result resource_barrier(TextureHandle handle, const ResourceBarrierDesc desc) override;
        Result clear_render_target(ViewHandle handle, const float clearColor[4]) override;
        Result clear_depth_stencil(ViewHandle handle, float depth, uint8_t stencil) override;
        Result set_viewport_scissor(uint32_t width, uint32_t height) override;
        Result set_primitive_topology(PrimitiveTopologyType topology) override;
        Result set_graphics_pipeline(PipelineStateHandle handle) override;
        Result set_32bit_constant(uint32_t rootParameterIndex, uint32_t value) override;
        Result set_cbv(uint32_t rootParameterIndex, BufferHandle handle) override;
        Result set_srv(uint32_t rootParameterIndex, BufferHandle handle) override;
        Result set_uav(uint32_t rootParameterIndex, BufferHandle handle) override;
        Result set_graphics_descriptor_table(uint32_t rootParameterIndex, ViewHandle handle) override;
        Result set_render_targets(const ViewHandle* renderTargetViews, uint32_t renderTargetCount, ViewHandle depthStencilView) override;
        Result draw_instanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation) override;
        Result draw_indexed_instanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation) override;
    private:
        Result create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        Result create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        Result resolve_slice_index(size_t sliceCount, uint32_t& outIndex) const;
        Result resolve_root_descriptor_buffer(BufferHandle handle, DX12GpuResource** outResource) const;
    private:
        DescriptorAllocator& m_descriptorAllocator; // デスクリプタアロケータへの参照
        DX12BufferManager& m_bufferManager; // バッファマネージャへの参照
        DX12TextureManager& m_textureManager; // テクスチャマネージャへの参照
        DX12ViewManager& m_viewManager; // ビューマネージャへの参照
        DX12PipelineManager& m_pipelineManager; // パイプラインマネージャへの参照
        ComPtr<ID3D12GraphicsCommandList> m_commandList = nullptr;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator = nullptr;
        CommandListType m_type = CommandListType::Graphics;
        uint32_t m_frameIndex = 0; // コマンドコンテキストが属するフレームのインデックス（リングバッファ管理用）
        uint32_t m_bufferCount = 1; // フレームリング全体のバッファ数
    };

    class DX12CommandPool final : public ICommandPool
    {
    public:
        DX12CommandPool(DX12RenderDevice& renderDevice,
            DescriptorAllocator& descriptorAllocator,
            DX12BufferManager& bufferManager,
            DX12TextureManager& textureManager,
            DX12ViewManager& viewManager,
            DX12PipelineManager& pipelineManager)
            : m_renderDevice(renderDevice),
            m_descriptorAllocator(descriptorAllocator),
            m_bufferManager(bufferManager),
            m_textureManager(textureManager),
            m_viewManager(viewManager),
            m_pipelineManager(pipelineManager),
            m_graphicsContextPool(
                32,
                [](DX12GpuCommandContext& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device(), &descriptorAllocator, &bufferManager, &textureManager, &viewManager, &pipelineManager]()
                {
                    return std::make_unique<DX12GpuCommandContext>(*d3d12Device, descriptorAllocator, bufferManager, textureManager, viewManager, pipelineManager, D3D12_COMMAND_LIST_TYPE_DIRECT);
                })
            , m_computeContextPool(
                32,
                [](DX12GpuCommandContext& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device(), &descriptorAllocator, &bufferManager, &textureManager, &viewManager, &pipelineManager]()
                {
                    return std::make_unique<DX12GpuCommandContext>(*d3d12Device, descriptorAllocator, bufferManager, textureManager, viewManager, pipelineManager, D3D12_COMMAND_LIST_TYPE_COMPUTE);
                })
            , m_copyContextPool(
                32,
                [](DX12GpuCommandContext& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device(), &descriptorAllocator, &bufferManager, &textureManager, &viewManager, &pipelineManager]()
                {
                    return std::make_unique<DX12GpuCommandContext>(*d3d12Device, descriptorAllocator, bufferManager, textureManager, viewManager, pipelineManager, D3D12_COMMAND_LIST_TYPE_COPY);
                })
        {}
        ~DX12CommandPool() override = default;

        Result get_command_context(CommandListType type, CommandContextLease& outContext) override;
        Result return_command_context(CommandContextLease& context) override;
    private:
        DX12RenderDevice& m_renderDevice;
        DescriptorAllocator& m_descriptorAllocator;
        DX12BufferManager& m_bufferManager; // バッファマネージャへの参照
        DX12TextureManager& m_textureManager; // テクスチャマネージャへの参照
        DX12ViewManager& m_viewManager; // ビューマネージャへの参照
        DX12PipelineManager& m_pipelineManager; // パイプラインマネージャへの参照
        Core::Pool<DX12GpuCommandContext, std::function<void(DX12GpuCommandContext&)>> m_graphicsContextPool;
        std::mutex m_graphicsPoolMutex;
        Core::Pool<DX12GpuCommandContext, std::function<void(DX12GpuCommandContext&)>> m_computeContextPool;
        std::mutex m_computePoolMutex;
        Core::Pool<DX12GpuCommandContext, std::function<void(DX12GpuCommandContext&)>> m_copyContextPool;
        std::mutex m_copyPoolMutex;
    };

    class DX12GpuCommandQueue final : public IQueueContext
    {
    public:
        DX12GpuCommandQueue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        // コピー禁止
        DX12GpuCommandQueue(const DX12GpuCommandQueue&) = delete;
        DX12GpuCommandQueue& operator=(const DX12GpuCommandQueue&) = delete;
        // ムーブは許可
        DX12GpuCommandQueue(DX12GpuCommandQueue&&) = default;
        DX12GpuCommandQueue& operator=(DX12GpuCommandQueue&&) = default;
        ~DX12GpuCommandQueue() override = default;

        CommandListType type() const override;
        Result submit(std::vector<ICommandContext*>& contexts) override;
        Result signal() override;
        Result wait() override;
        Result wait_for_queue(IQueueContext& queue) override;

        // --- 取得 ---
        ID3D12CommandQueue* command_queue() const { return m_commandQueue.Get(); }
    private:
        Result create_fence(ID3D12Device& device);
        Result create_fence_event();
        Result create_command_queue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
    private:
        ComPtr<ID3D12CommandQueue> m_commandQueue = nullptr;
        ComPtr<ID3D12Fence> m_fence = nullptr;
        HANDLE m_fenceEvent = {};
        uint64_t m_fenceValue = {};
        CommandListType m_type = CommandListType::Graphics;
    };

    class DX12QueuePool final : public IQueuePool
    {
    public:
        DX12QueuePool(DX12RenderDevice& renderDevice)
            : m_renderDevice(renderDevice),
            m_graphicsQueuePool(
                1,
                [](DX12GpuCommandQueue& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandQueue>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
                })
            , m_computeQueuePool(
                4,
                [](DX12GpuCommandQueue& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandQueue>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
                })
            , m_copyQueuePool(
                4,
                [](DX12GpuCommandQueue& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandQueue>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_COPY);
                })
        {
        }
        ~DX12QueuePool() override = default;
        Result get_queue_context(CommandListType type, QueueContextLease& outContext) override;
        Result return_queue_context(QueueContextLease& context) override;
        Result wait_for_graphics_queue() override;
    private:
        DX12RenderDevice& m_renderDevice;
        Core::Pool<DX12GpuCommandQueue, std::function<void(DX12GpuCommandQueue&)>> m_graphicsQueuePool;
        std::mutex m_graphicsPoolMutex;
        Core::Pool<DX12GpuCommandQueue, std::function<void(DX12GpuCommandQueue&)>> m_computeQueuePool;
        std::mutex m_computePoolMutex;
        Core::Pool<DX12GpuCommandQueue, std::function<void(DX12GpuCommandQueue&)>> m_copyQueuePool;
        std::mutex m_copyPoolMutex;
    };
}
