#include "DX12RenderDevice.h"

namespace Cue::RHI::DX12
{
    Result DX12RenderDevice::initialize(bool a_enableDebugLayer)
    {
        // DXGI factory を先に作り、同じ factory から adapter 列挙と swap chain 作成を行う。
        // device だけを先に作ると、後続で factory/adapter の整合を取りにくくなる。
        // DXGI を先に作らないと、アダプタ列挙とデバイス生成の入口が用意できません
        Result r = create_dxgi_factory(a_enableDebugLayer);
        if (!r)
        {
            return r;
        }

        // 選択済みファクトリを基準にデバイスを作って、後段が同じ実体を見るようにし
        r = create_d3d12_device();
        if (!r)
        {
            return r;
        }
        return Result::ok();
    }

    Result DX12RenderDevice::create_dxgi_factory(bool a_enableDebugLayer)
    {
#ifndef CUE_RELEASE
        // 開発時だけデバッグレイヤーを噛ませて、実行時コストをリリースに持ち込みません
        ComPtr<ID3D12Debug6> debugController;
        if (a_enableDebugLayer)
        {
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();
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

        // DXGI 側のデバッグフラグも揃えて、生成物と検証設定の不一致を避ける
        HRESULT hr = CreateDXGIFactory2(
            a_enableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0,
            IID_PPV_ARGS(&m_dxgiFactory));
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr), Severity::Fatal,
                "Failed to create DXGI factory.");
        }

        set_dxgi_name(m_dxgiFactory.Get(), L"DX12RenderDevice_DXGIFactory");

        return Result::ok();
    }

    Result DX12RenderDevice::create_d3d12_device()
    {
        // Windows が報告する高性能 GPU 順に試し、software adapter は候補から外す。
        // 生成に成功した最初の feature level を採用する。
        // 高性能 GPU 優先で列挙し、意図しないソフトウェアデバイス選択を避ける
        HRESULT hr = S_OK;
        for (UINT i = 0; m_dxgiFactory->EnumAdapterByGpuPreference(i,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&m_adapter)) !=
            DXGI_ERROR_NOT_FOUND; ++i)
        {
            // 条件判定用アダプタ情報取得
            hr = m_adapter->GetDesc3(&m_adapterDesc);
            if (FAILED(hr))
            {
                return Result::fail(
                    PAL::Win::convert_hresult_code(hr), Severity::Warning,
                    "Failed to get adapter description. Skipping this adapter.");
            }

            if (!(m_adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
            {
                break;
            }

            m_adapter = nullptr;
        }

        if (m_adapter == nullptr)
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

        // 高い機能レベルから試して、利用可能な最大機能をそのまま採用し
        for (size_t i = 0; i < _countof(featureLevels); ++i)
        {
            hr = D3D12CreateDevice(m_adapter.Get(), featureLevels[i], IID_PPV_ARGS(&m_d3d12Device));

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

        set_d3d12_name(m_d3d12Device.Get(), L"DX12RenderDevice_D3D12Device");

#ifndef CUE_RELEASE
        ComPtr<ID3D12InfoQueue> infoQueue;

        // 重大メッセージだけを残すと、通常ログに埋もれず原因特定が速くなる
        if (SUCCEEDED(m_d3d12Device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
        {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
            infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_FENCE_ZERO_WAIT, TRUE);

            D3D12_MESSAGE_ID hide[] =
            {
                // windows 11 の dxgi と dx12 デバッグレイヤー相互作用エラー
                // https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
                D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
                D3D12_MESSAGE_ID_GPU_BASED_VALIDATION_RESOURCE_STATE_IMPRECISE,
                D3D12_MESSAGE_ID_GPU_BASED_VALIDATION_INCOMPATIBLE_TEXTURE_LAYOUT,
                D3D12_MESSAGE_ID_FENCE_ZERO_WAIT // imgui 起因メッセージ
            };

            D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = _countof(hide);
            filter.DenyList.pIDList = hide;
            filter.DenyList.NumSeverities = _countof(severities);
            filter.DenyList.pSeverityList = severities;

            infoQueue->PushStorageFilter(&filter);
        }
#endif // CUE_RELEASE

        return Result::ok();
    }
}
