#pragma once

/// *********************************************************************************
/// GPU プロファイラー
/// *********************************************************************************

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === RHI includes ===
#include <RHICommon.h>

// === DirectX 12 includes ===
#include "DX12Common.h"
#include "DX12RenderDevice.h"

namespace Cue::RHI::DX12
{
    /// @brief GPU プロファイラー。GPU 上のイベントマーカーを管理する。
    class DX12GpuProfiler final
    {
    public:
        DX12GpuProfiler(DX12RenderDevice& a_renderDevice) noexcept
            : m_renderDevice(a_renderDevice)
        {}
        ~DX12GpuProfiler() = default;

        Result get_gpu_memory_usage(
            GpuMemoryUsage& a_out)
        {
            IDXGIAdapter4* adapter = m_renderDevice.get_adapter();
            if (adapter == nullptr)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error, "Adapter is null.");
            }

            // ローカルGPUメモリ情報を取得する
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};

            const HRESULT hr = adapter->QueryVideoMemoryInfo(
                0,
                DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                &info);

            if (FAILED(hr))
            {
                return Result::fail(PAL::Win::convert_hresult_code(hr), Severity::Error, "Failed to query video memory info.");
            }

            // 値を返す
            a_out.budgetBytes = static_cast<uint64_t>(info.Budget);
            a_out.currentUsageBytes = static_cast<uint64_t>(info.CurrentUsage);
            a_out.availableForReservationBytes = static_cast<uint64_t>(info.AvailableForReservation);
            a_out.currentReservationBytes = static_cast<uint64_t>(info.CurrentReservation);

            return Result::ok();
        }
    private:
        DX12RenderDevice& m_renderDevice;
    };
}
