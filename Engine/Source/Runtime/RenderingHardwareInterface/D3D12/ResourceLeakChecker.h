#pragma once

/// ************************************************************************************
/// D3D12リソースリークチェッカー
/// ************************************************************************************

// === DirectX 12 includes ===
#include "DX12Common.h"

namespace Cue::RHI::DX12
{
    /// @brief D3D12/DXGI の live object をプロセス終了前に出力する補助クラス。
    /// @details D3D12Backend が最初に保持し最後に破棄することで、
    ///          他の D3D12 object 破棄後に残存 object を確認できる。
    class ResourceLeakChecker final
    {
    public:
        // デストラクタ
        ~ResourceLeakChecker()
        {
#ifndef NDEBUG
            // デバッグ時のみリーク情報を出力して問題を早期発見する
            // DXGI 全体と D3D12 の両方を確認する
            ComPtr<IDXGIDebug1> debug;
            if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
            {
                debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
                debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
                debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
            }
#endif
        }
    };
}
