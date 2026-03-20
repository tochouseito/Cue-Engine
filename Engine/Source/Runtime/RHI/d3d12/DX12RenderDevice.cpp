#include "DX12RenderDevice.h"

namespace Cue::RHI::DX12
{
    Result DX12RenderDevice::initialize(bool enableDebugLayer)
    {
        // DXGI ファクトリの生成
        Result r = create_dxgi_factory(enableDebugLayer);
        if (!r)
        {
            return r;
        }

        // D3D12 デバイスの生成
        r = create_d3d12_device();
        if (!r)
        {
            return r;
        }
        return Result::ok();
    }
    Result DX12RenderDevice::create_dxgi_factory(bool enableDebugLayer)
    {
#ifndef CUE_RELEASE
        // デバッグ検証設定
        ComPtr<ID3D12Debug6> debugController;
        if (enableDebugLayer)
        {
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                // デバッグレイヤー有効化
                debugController->EnableDebugLayer();

                // gpu 側検証有効化
                debugController->SetEnableGPUBasedValidation(true);
            }
        }
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> deviceRemoved;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&deviceRemoved))))
        {
            deviceRemoved->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            deviceRemoved->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
#endif // CUE_RELEASE

        // DXGI ファクトリの生成
        HRESULT hr = CreateDXGIFactory2(
            enableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0,
            IID_PPV_ARGS(&m_dxgiFactory));
        if(FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr), Severity::Fatal,
                "Failed to create DXGI factory.");
        }
        SetDXGIName(m_dxgiFactory.Get(), L"DX12RenderDevice_DXGIFactory");

        return Result::ok();
    }
    Result DX12RenderDevice::create_d3d12_device()
    {
        // 高性能 GPU を優先してアダプタを列挙する
        HRESULT hr = S_OK;
        ComPtr<IDXGIAdapter4> adapter = nullptr;
        for (UINT i = 0; m_dxgiFactory->EnumAdapterByGpuPreference(i,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) !=
            DXGI_ERROR_NOT_FOUND; ++i)
        {
            // 条件判定用アダプタ情報取得
            hr = adapter->GetDesc3(&m_adapterDesc);
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr), Severity::Warning,
                    "Failed to get adapter description. Skipping this adapter.");
            }

            // ソフトウェアアダプタを除外
            if (!(m_adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
            {
                break;
            }

            // 次候補へ移動
            adapter = nullptr;
        }

        // 適切なアダプタ未検出
        if (adapter == nullptr)
        {
            return Result::fail(
                Code::NotFound, Severity::Fatal,
                "Failed to find a suitable GPU adapter.");
        }

        // 候補機能レベル列挙
        constexpr const D3D_FEATURE_LEVEL featureLevels[] =
        {
            D3D_FEATURE_LEVEL_12_2,
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        constexpr const char* featureLevelNames[] =
        {
            "12.2",
            "12.1",
            "12.0",
            "11.1",
            "11.0",
        };

        // アダプタでサポートされている最高の機能レベル優先で D3D12 デバイスを生成する
        for (size_t i = 0; i < _countof(featureLevels); ++i)
        {

            // 候補アダプタでデバイス生成
            hr = D3D12CreateDevice(adapter.Get(), featureLevels[i], IID_PPV_ARGS(&m_d3d12Device));

            // 成功時に採用確定
            if (SUCCEEDED(hr))
            {
                m_featureLevel = featureLevels[i];
                break;
            }
        }

        // デバイス生成失敗
        if (!m_d3d12Device)
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr), Severity::Fatal,
                "Failed to create D3D12 device with the selected GPU adapter.");
        }

        SetD3D12Name(m_d3d12Device.Get(), L"DX12RenderDevice_D3D12Device");

#ifndef CUE_RELEASE
        ComPtr<ID3D12InfoQueue> infoQueue;
        // 重要メッセージ保持用フィルタ設定
        if (SUCCEEDED(m_d3d12Device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
        {
            // 致命エラーで停止
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

            // エラーで停止
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

            // 警告で停止
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

            // info で停止 (特定の message id を指定して停止させる)
            infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_FENCE_ZERO_WAIT, TRUE);

            // 抑制対象 message id
            D3D12_MESSAGE_ID hide[] = {
                // windows 11 の dxgi と dx12 デバッグレイヤー相互作用エラー
                // https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
                D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
                D3D12_MESSAGE_ID_GPU_BASED_VALIDATION_RESOURCE_STATE_IMPRECISE,
                D3D12_MESSAGE_ID_FENCE_ZERO_WAIT // imgui 起因メッセージ
            };

            // 抑制対象レベル
            D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = _countof(hide);
            filter.DenyList.pIDList = hide;
            filter.DenyList.NumSeverities = _countof(severities);
            filter.DenyList.pSeverityList = severities;

            // 指定メッセージ表示抑制
            infoQueue->PushStorageFilter(&filter);
        }
#endif // CUE_RELEASE

        return Result::ok();
    }
}
