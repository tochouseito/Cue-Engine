// ResourceLeakChecker の役割と公開要素を定義する

#pragma once

// === DirectX 12 includes ===
#include "stdafx.h"

namespace Cue::RHI::DX12
{
    class ResourceLeakChecker final
    {
    public:
        // デストラクタ
        ~ResourceLeakChecker()
        {
#ifndef NDEBUG
            // デバッグ時のみリーク情報を出力して問題を早期発見する
            // DXGI 全体と D3D12 の両方を確認する
            comPtr<IDXGIDebug1> debug;
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
