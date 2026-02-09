#include "GpuCommand.h"

namespace Cue::GraphicsCore::DX12
{
    CommandContext::CommandContext(ID3D12Device& device)
        : m_device(device)
    {
    }
    Result CommandContext::initialize(D3D12_COMMAND_LIST_TYPE type)
    {
        Result r;
        // 1) コマンドアロケータの作成
        r = create_command_allocator(type);
        if (!r)
        {
            return r;
        }
        // 2) コマンドリストの作成
        r = create_command_list(type);
        if (!r)
        {
            return r;
        }
        return Result::ok();
    }
    Result CommandContext::reset()
    {
        // 1) コマンドアロケータのリセット
        HRESULT hr = m_commandAllocator->Reset();
        if (FAILED(hr))
        {
            return Result::fail(
                Core::Facility::Graphics,
                Core::Code::InvalidState,
                Core::Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to reset CommandAllocator.");
        }
        // 2) コマンドリストのリセット
        hr = m_commandList->Reset(
            m_commandAllocator.Get(),
            nullptr);
        if (FAILED(hr))
        {
            return Result::fail(
                Core::Facility::Graphics,
                Core::Code::InvalidState,
                Core::Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to reset CommandList.");
        }
        return Result::ok();
    }
    Result CommandContext::close()
    {
        // コマンドリストのクローズ
        HRESULT hr = m_commandList->Close();
        if (FAILED(hr))
        {
            return Result::fail(
                Core::Facility::Graphics,
                Core::Code::InvalidState,
                Core::Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to close CommandList.");
        }
        return Result::ok();
    }
    Result CommandContext::create_command_allocator(D3D12_COMMAND_LIST_TYPE type)
    {
        // コマンドアロケータの作成
        HRESULT hr = m_device.CreateCommandAllocator(
            type,
            IID_PPV_ARGS(&m_commandAllocator));
        if (FAILED(hr))
        {
            return Result::fail(
                Core::Facility::Graphics,
                Core::Code::CreationFailed,
                Core::Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandAllocator.");
        }
        SetD3D12Name(m_commandAllocator.Get(), L"CommandContext CommandAllocator");
        return Result::ok();
    }
    Result CommandContext::create_command_list(D3D12_COMMAND_LIST_TYPE type)
    {
        // 1) コマンドリストの作成
        HRESULT hr = m_device.CreateCommandList(
            0,
            type,
            m_commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&m_commandList));
        if (FAILED(hr))
        {
            return Result::fail(
                Core::Facility::Graphics,
                Core::Code::CreationFailed,
                Core::Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandList.");
        }
        SetD3D12Name(m_commandList.Get(), L"CommandContext CommandList");

        // 2) コマンドリストは生成直後にオープン状態になるのでクローズしておく
        m_commandList->Close();
        return Result::ok();
    }
} // namespace Cue::GraphicsCore::DX12
