#include "RenderDevice.h"

namespace Cue::GraphicsCore::DX12
{
    Result RenderDevice::initialize(bool enableDebugLayer)
    {
        Result r;
        // 1) DXGIファクトリ生成
        r = create_dxgi_factory(enableDebugLayer);
        if (!r)
        {
            return r;
        }
        // 2) D3D12デバイス生成
        r = create_d3d12_device();
        if (!r)
        {
            return r;
        }
        return Result::ok();
    }
    Result RenderDevice::create_dxgi_factory(bool enableDebugLayer)
    {
        // 1) デバッグ時は検証を強めて問題の早期発見を狙う
        // 2) DXGI ファクトリを生成して基盤を確立する
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
                // デバッグレイヤーを有効化する
                debugController->EnableDebugLayer();

                // さらにGPU側でもチェックを行うようにする
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
                Core::Facility::Graphics,
                Core::Code::CreationFailed,
                Core::Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create DXGI Factory.");
        }
        SetDXGIName(m_dxgiFactory.Get(), L"RenderDevice_DXGIFactory");
        return Result::ok();
    }
    Result RenderDevice::create_d3d12_device()
    {
        // 1) 高性能 GPU を優先してアダプタを探索する
        // 2) 利用可能な機能レベルでデバイスを生成する
        HRESULT hr;

        // 未選択状態を明確にするため nullptr で初期化する
        Microsoft::WRL::ComPtr < IDXGIAdapter4> useAdapter = nullptr;

        // 高性能優先で探索するため優先度順に列挙する
        for (UINT i = 0; m_dxgiFactory->EnumAdapterByGpuPreference(i,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
            DXGI_ERROR_NOT_FOUND; ++i)
        {

            // 条件判定のためアダプタ情報を取得する
            hr = useAdapter->GetDesc3(&m_adapterDesc);

            if (FAILED(hr))
            {
                return Result::fail(
                    Core::Facility::Graphics,
                    Core::Code::GettingInfoFailed,
                    Core::Severity::Error,
                    static_cast<uint32_t>(hr),
                    "Failed to get adapter description.");
            }

            // ソフトウェアアダプタは避けたいのでスキップする
            if (!(m_adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
            {
                break;
            }
            // 使えない場合は次の候補へ進む
            useAdapter = nullptr;
        }
        // 適切なアダプタが見つからなかったので起動できない
        if (useAdapter == nullptr)
        {
            Result::fail(
                Core::Facility::Graphics,
                Core::Code::NotFound,
                Core::Severity::Error,
                0,
                "Failed to find a suitable GPU adapter.");
        }

        // 選択可能な機能レベルを高い順に用意する
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

        // できるだけ高い機能レベルを確保する
        for (size_t i = 0; i < _countof(featureLevels); ++i)
        {

            // 採用したアダプターでデバイスを生成する
            hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&m_d3d12Device));

            // 生成できたらそこで確定する
            if (SUCCEEDED(hr))
            {
                m_featureLevel = featureLevels[i];
                break;
            }
        }
        // デバイスの生成がうまくいかなかったので起動できない
        if (m_d3d12Device == nullptr)
        {
            return Result::fail(
                Core::Facility::Graphics,
                Core::Code::CreationFailed,
                Core::Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create D3D12 Device.");
        }

        // デバッグ用に名前を付ける
        SetD3D12Name(m_d3d12Device.Get(), L"RenderDevice_D3D12Device");
#ifndef CUE_RELEASE
        ComPtr<ID3D12InfoQueue> infoQueue;
        // 重要メッセージが欠落しないよう一時的にフィルタを調整する

        if (SUCCEEDED(m_d3d12Device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
        {
            // ヤバいエラー時に止まる
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

            // エラー時に止まる
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

            // 警告時に止まる
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

            infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_FENCE_ZERO_WAIT, TRUE);

            // 抑制するメッセージのID
            D3D12_MESSAGE_ID denyIds[] = {

                // Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
                // https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
                D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
                D3D12_MESSAGE_ID_GPU_BASED_VALIDATION_RESOURCE_STATE_IMPRECISE,  // = 1044 相当
                D3D12_MESSAGE_ID_FENCE_ZERO_WAIT // ImGuiが原因で出る。無視してよい
            };

            // 抑制するレベル
            D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;
            filter.DenyList.NumSeverities = _countof(severities);
            filter.DenyList.pSeverityList = severities;

            // 指定したメッセージの表示を抑制する
            infoQueue->PushStorageFilter(&filter);
        }
#endif // DEBUG
        return Result::ok();
    }
}
