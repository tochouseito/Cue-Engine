#include "GpuCommand.h"

namespace Cue::GraphicsCore::DX12
{
    Result CommandContext::initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
    {
        m_device = device;
        if (m_device == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Device is null.");
        }

        // 1) 初期化済みなら再生成は不要
        if (m_commandAllocator && m_commandList)
        {
            return Result::ok();
        }

        Result r;
        // 2) コマンドアロケータの作成
        r = create_command_allocator(type);
        if (!r)
        {
            return r;
        }
        // 3) コマンドリストの作成
        r = create_command_list(type);
        if (!r)
        {
            return r;
        }
        return Result::ok();
    }
    Result CommandContext::reset()
    {
        if (!m_commandAllocator || !m_commandList)
        {
            return Result::ok();
        }

        // 1) コマンドアロケータのリセット
        HRESULT hr = m_commandAllocator->Reset();
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
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
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to reset CommandList.");
        }
        return Result::ok();
    }
    Result CommandContext::close()
    {
        if (!m_commandList)
        {
            return Result::ok();
        }

        // コマンドリストのクローズ
        HRESULT hr = m_commandList->Close();
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to close CommandList.");
        }
        return Result::ok();
    }
    Result CommandContext::create_command_allocator(D3D12_COMMAND_LIST_TYPE type)
    {
        if (m_device == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Device is null.");
        }

        // コマンドアロケータの作成
        HRESULT hr = m_device->CreateCommandAllocator(
            type,
            IID_PPV_ARGS(&m_commandAllocator));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandAllocator.");
        }
        SetD3D12Name(m_commandAllocator.Get(), L"CommandContext CommandAllocator");
        return Result::ok();
    }
    Result CommandContext::create_command_list(D3D12_COMMAND_LIST_TYPE type)
    {
        if (m_device == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Device is null.");
        }

        // 1) コマンドリストの作成
        HRESULT hr = m_device->CreateCommandList(
            0,
            type,
            m_commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&m_commandList));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandList.");
        }
        SetD3D12Name(m_commandList.Get(), L"CommandContext CommandList");

        // 2) コマンドリストは生成直後にオープン状態になるのでクローズしておく
        m_commandList->Close();
        return Result::ok();
    }
} // namespace Cue::GraphicsCore::DX12
