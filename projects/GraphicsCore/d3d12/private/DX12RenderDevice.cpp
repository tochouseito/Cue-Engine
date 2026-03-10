#include "DX12RenderDevice.h"

namespace Cue::GraphicsCore::DX12
{
    Result DX12RenderDevice::initialize(bool enableDebugLayer)
    {
        Result r;
        // 1) dxgi ファクトリ生成
        r = create_dxgi_factory(enableDebugLayer);
        if (!r)
        {
            return r;
        }
        // 2) d3d12 デバイス生成
        r = create_d3d12_device();
        if (!r)
        {
            return r;
        }
        return Result::ok();
    }
    Result DX12RenderDevice::create_dxgi_factory(bool enableDebugLayer)
    {
        // 1) デバッグ検証設定
        // 2) dxgi ファクトリ生成
#ifndef CUE_RELEASE
        /*
        [ INITIALIZATION MESSAGE #1016: CREATEDEVICE_DEBUG_LAYER_STARTUP_OPTIONS]
        Debug時のみの警告なため無視
        */
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
#endif
        HRESULT hr;
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_dxgiFactory));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create DXGI Factory.");
        }
        SetDXGIName(m_dxgiFactory.Get(), L"RenderDevice_DXGIFactory");
        return Result::ok();
    }
    Result DX12RenderDevice::create_d3d12_device()
    {
        // 1) 高性能 gpu 優先でアダプタ探索
        // 2) 利用可能機能レベルでデバイス生成
        HRESULT hr;

        // 未選択状態で初期化
        Microsoft::WRL::ComPtr < IDXGIAdapter4> useAdapter = nullptr;

        // 優先度順でアダプタ列挙
        for (UINT i = 0; m_dxgiFactory->EnumAdapterByGpuPreference(i,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
            DXGI_ERROR_NOT_FOUND; ++i)
        {

            // 条件判定用アダプタ情報取得
            hr = useAdapter->GetDesc3(&m_adapterDesc);

            if (FAILED(hr))
            {
                return Result::fail(
                    Facility::Graphics,
                    Code::GettingInfoFailed,
                    Severity::Error,
                    static_cast<uint32_t>(hr),
                    "Failed to get adapter description.");
            }

            // ソフトウェアアダプタを除外
            if (!(m_adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
            {
                break;
            }
            // 次候補へ移動
            useAdapter = nullptr;
        }
        // 適切なアダプタ未検出
        if (useAdapter == nullptr)
        {
            Result::fail(
                Facility::Graphics,
                Code::NotFound,
                Severity::Error,
                0,
                "Failed to find a suitable GPU adapter.");
        }

        // 候補機能レベル列挙
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_12_2,
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
        };
        const char* featureLevelStrings[] = {
            "12.2",
            "12.1",
            "12.0"
        };

        // 高機能レベル優先で生成
        for (size_t i = 0; i < _countof(featureLevels); ++i)
        {

            // 候補アダプタでデバイス生成
            hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&m_d3d12Device));

            // 成功時に採用確定
            if (SUCCEEDED(hr))
            {
                m_featureLevel = featureLevels[i];
                break;
            }
        }
        // デバイス生成失敗
        if (m_d3d12Device == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create D3D12 Device.");
        }

        // デバッグ名設定
        SetD3D12Name(m_d3d12Device.Get(), L"RenderDevice_D3D12Device");
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

            infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_FENCE_ZERO_WAIT, TRUE);

            // 抑制対象 message id
            D3D12_MESSAGE_ID denyIds[] = {

                // windows 11 の dxgi と dx12 デバッグレイヤー相互作用エラー
                // https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
                D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
                D3D12_MESSAGE_ID_GPU_BASED_VALIDATION_RESOURCE_STATE_IMPRECISE,  // = 1044 相当
                D3D12_MESSAGE_ID_FENCE_ZERO_WAIT // imgui 起因メッセージ
            };

            // 抑制対象レベル
            D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;
            filter.DenyList.NumSeverities = _countof(severities);
            filter.DenyList.pSeverityList = severities;

            // 指定メッセージ表示抑制
            infoQueue->PushStorageFilter(&filter);
        }
#endif // DEBUG
        return Result::ok();
    }
}
