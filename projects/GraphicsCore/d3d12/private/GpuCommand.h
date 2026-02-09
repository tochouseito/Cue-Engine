#pragma once
#include "stdafx.h"
#include <Pool.h>

#include <functional>
#include <mutex>

namespace Cue::GraphicsCore::DX12
{
    class CommandContext
    {
    public:
        CommandContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        virtual ~CommandContext() = default;
        Result initialize(D3D12_COMMAND_LIST_TYPE type);
        Result reset();
        Result close();
        ID3D12GraphicsCommandList* get_command_list() { return m_commandList.Get(); }
        ID3D12CommandAllocator* get_command_allocator() { return m_commandAllocator.Get(); }
        bool is_list_empty() const { return m_listEmpty; }

        /* === Commands === */
    private:
        Result create_command_allocator(D3D12_COMMAND_LIST_TYPE type);
        Result create_command_list(D3D12_COMMAND_LIST_TYPE type);
    protected:
        ID3D12Device& m_device;
        bool m_listEmpty = true;
        ComPtr<ID3D12GraphicsCommandList> m_commandList = nullptr;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator = nullptr;
    };

    class GraphicsCommandContext : public CommandContext
    {
    public:
        GraphicsCommandContext(ID3D12Device& device)
            :CommandContext(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
        }
        ~GraphicsCommandContext() = default;
    };

    class ComputeCommandContext : public CommandContext
    {
    public:
        ComputeCommandContext(ID3D12Device& device)
            :CommandContext(device, D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
        }
        ~ComputeCommandContext() = default;
    };

    class CopyCommandContext : public CommandContext
    {
    public:
        CopyCommandContext(ID3D12Device& device)
            :CommandContext(device, D3D12_COMMAND_LIST_TYPE_COPY)
        {
        }
        ~CopyCommandContext() = default;
    };

    class CommandPool final
    {
    public:
        /// @brief コンストラクタ
        CommandPool(ID3D12Device& device)
            : m_device(device)
        {
        }
        /// @brief デストラクタ
        ~CommandPool() = default;
    private:
        ID3D12Device& m_device;

        Core::Pool<GraphicsCommandContext, std::function<void(GraphicsCommandContext&)>> m_graphicsContextPool{
            32,
            [](GraphicsCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_graphicsContextPoolMutex;

        Core::Pool<ComputeCommandContext, std::function<void(ComputeCommandContext&)>> m_computeContextPool{
            32,
            [](ComputeCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_computeContextPoolMutex;

        Core::Pool<CopyCommandContext, std::function<void(CopyCommandContext&)>> m_copyContextPool{
            32,
            [](CopyCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_copyContextPoolMutex;
    };
} // namespace Cue::GraphicsCore::DX12
