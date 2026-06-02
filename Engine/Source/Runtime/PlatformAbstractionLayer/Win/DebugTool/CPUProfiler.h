#pragma once

/// *********************************************************************************
/// CPU プロファイラー
/// *********************************************************************************

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Win includes ===
#include "WinCommon.h"

// === C++ includes ===
#include <cstdint>

namespace Cue::PAL::Win
{
    class CPUProfiler final
    {
    public:
        CPUProfiler() noexcept = default;
        ~CPUProfiler() = default;

        /// @brief プロセスのメモリ使用量を取得する
        /// @param a_out 
        /// @return 
        Result get_process_memory_usage(ProcessMemoryUsage& a_out) const
        {
            // プロセスのメモリカウンタを取得する
            PROCESS_MEMORY_COUNTERS_EX counters{};
            counters.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);

            const BOOL result = GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                sizeof(PROCESS_MEMORY_COUNTERS_EX));

            if (result == FALSE)
            {
                return Result::fail(
                    Code::GetFailed, Severity::Error, "Failed to get process memory info");
            }

            // 必要な値だけ返す
            a_out.workingSetBytes = static_cast<uint64_t>(counters.WorkingSetSize);
            a_out.privateBytes = static_cast<uint64_t>(counters.PrivateUsage);

            return Result::ok();
        }

        /// @brief システムのメモリ使用量を取得する
        /// @param a_out 
        /// @return 
        Result get_system_memory_usage(SystemMemoryUsage& a_out) const
        {
            // システムメモリ情報を取得する
            MEMORYSTATUSEX status{};
            status.dwLength = sizeof(MEMORYSTATUSEX);

            if (GlobalMemoryStatusEx(&status) == FALSE)
            {
                return Result::fail(
                    Code::GetFailed, Severity::Error, "Failed to get system memory info");
            }

            // 必要な値だけ返す
            a_out.totalPhysBytes = static_cast<uint64_t>(status.ullTotalPhys);
            a_out.availPhysBytes = static_cast<uint64_t>(status.ullAvailPhys);
            a_out.memoryLoadPercent = static_cast<uint32_t>(status.dwMemoryLoad);

            return Result::ok();
        }
    };
}
