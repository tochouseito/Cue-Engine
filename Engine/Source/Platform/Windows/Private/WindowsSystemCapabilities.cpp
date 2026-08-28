#include <Cue/Platform/Windows/WindowsPlatform.h>

#include "SystemCapabilityMapper.h"
#include "WindowsUtilities.h"

#include <Cue/Foundation/Assert.h>

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>

namespace
{
constexpr std::int64_t k_memoryQueryFailed = 20;
constexpr std::int64_t k_cacheQueryFailed = 21;
constexpr std::int64_t k_processorCountQueryFailed = 22;

/// @brief Allocation失敗を追加AllocationなしでFatal終了境界へ渡す
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("Windows System Capability allocation failed");
    std::abort();
}

/// @brief Win32 Architecture値をPlatform非依存Architectureへ変換する
[[nodiscard]] cue::SystemArchitecture map_architecture(WORD a_architecture) noexcept
{
    switch (a_architecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
        return cue::SystemArchitecture::X64;
    case PROCESSOR_ARCHITECTURE_ARM64:
        return cue::SystemArchitecture::Arm64;
    default:
        return cue::SystemArchitecture::Unknown;
    }
}

/// @brief Native Query失敗を同期Logし、全診断の配送失敗を集約する
void log_query_failure(const cue::AssertContext &a_context, std::int64_t a_code, const char *a_summary,
                       DWORD a_nativeCode, cue::LogResult &a_diagnosticResult) noexcept
{
    cue::Error error = cue::windows_private::make_native_error(a_context, a_code, a_summary, a_nativeCode);
    const cue::LogResult result = a_context.logger().log(cue::LogLevel::Error, a_summary, std::move(error));

    if (result != cue::LogResult::Success)
    {
        a_diagnosticResult = cue::LogResult::SinkFailure;
    }
}

/// @brief 全Processor GroupのActive Logical Processor数をQueryし、失敗をUnknown値と診断へ分離する
[[nodiscard]] cue::SystemCapabilityValue<std::uint32_t> query_logical_processor_count(
    const cue::AssertContext &a_context, cue::LogResult &a_diagnosticResult) noexcept
{
    const DWORD processorCount = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (processorCount != 0)
    {
        return cue::SystemCapabilityValue<std::uint32_t>::known(processorCount);
    }

    log_query_failure(a_context, k_processorCountQueryFailed, "Windows logical processor count query failed",
                      GetLastError(), a_diagnosticResult);
    return cue::SystemCapabilityValue<std::uint32_t>::query_failed();
}

/// @brief CPUIDとXCR0からCPU命令とOS Context SaveのSynthetic可能な入力を作る
[[nodiscard]] cue::windows_private::SystemCapabilityProbeInput query_cpu_capabilities() noexcept
{
    int maximumLeafRegisters[4] = {};
    __cpuid(maximumLeafRegisters, 0);
    const int maximumLeaf = maximumLeafRegisters[0];

    if (maximumLeaf < 1)
    {
        return {false, false, false, false, false, false, false, false, false, false, 0};
    }

    int featureRegisters[4] = {};
    __cpuidex(featureRegisters, 1, 0);
    const bool hasOsExtendedState = (featureRegisters[2] & (1 << 27)) != 0;
    const std::uint64_t extendedStateMask = hasOsExtendedState ? _xgetbv(0) : 0;
    bool hasAvx2 = false;

    if (maximumLeaf >= 7)
    {
        int extendedRegisters[4] = {};
        __cpuidex(extendedRegisters, 7, 0);
        hasAvx2 = (extendedRegisters[1] & (1 << 5)) != 0;
    }

    return {
        true,
        (featureRegisters[3] & (1 << 26)) != 0,
        (featureRegisters[2] & (1 << 0)) != 0,
        (featureRegisters[2] & (1 << 9)) != 0,
        (featureRegisters[2] & (1 << 19)) != 0,
        (featureRegisters[2] & (1 << 20)) != 0,
        (featureRegisters[2] & (1 << 28)) != 0,
        hasAvx2,
        (featureRegisters[2] & (1 << 12)) != 0,
        hasOsExtendedState,
        extendedStateMask,
    };
}

/// @brief Windows Logical Processor情報から最小の有効Cache Line Sizeを取得する
[[nodiscard]] cue::SystemCapabilityValue<std::uint32_t> query_cache_line_size(
    const cue::AssertContext &a_context, cue::LogResult &a_diagnosticResult) noexcept
{
    DWORD byteCount = 0;
    if (GetLogicalProcessorInformationEx(RelationCache, nullptr, &byteCount) != FALSE ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || byteCount == 0)
    {
        const DWORD nativeCode = GetLastError();
        log_query_failure(a_context, k_cacheQueryFailed, "Windows cache line query failed", nativeCode,
                          a_diagnosticResult);
        return cue::SystemCapabilityValue<std::uint32_t>::query_failed();
    }

    try
    {
        std::vector<std::byte> storage(byteCount);
        auto *information = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data());
        if (GetLogicalProcessorInformationEx(RelationCache, information, &byteCount) == FALSE)
        {
            log_query_failure(a_context, k_cacheQueryFailed, "Windows cache line query failed", GetLastError(),
                              a_diagnosticResult);
            return cue::SystemCapabilityValue<std::uint32_t>::query_failed();
        }

        std::uint32_t lineSize = std::numeric_limits<std::uint32_t>::max();
        std::size_t offset = 0;
        while (offset < byteCount)
        {
            const auto *entry =
                reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(storage.data() + offset);
            if (entry->Size == 0 || entry->Size > byteCount - offset)
            {
                log_query_failure(a_context, k_cacheQueryFailed, "Windows cache line query returned invalid data",
                                  ERROR_INVALID_DATA, a_diagnosticResult);
                return cue::SystemCapabilityValue<std::uint32_t>::query_failed();
            }

            if (entry->Relationship == RelationCache && entry->Cache.LineSize != 0)
            {
                lineSize = std::min(lineSize, static_cast<std::uint32_t>(entry->Cache.LineSize));
            }
            offset += entry->Size;
        }

        if (lineSize == std::numeric_limits<std::uint32_t>::max())
        {
            log_query_failure(a_context, k_cacheQueryFailed, "Windows cache line query returned no cache data",
                              ERROR_NOT_FOUND, a_diagnosticResult);
            return cue::SystemCapabilityValue<std::uint32_t>::query_failed();
        }
        return cue::SystemCapabilityValue<std::uint32_t>::known(lineSize);
    }
    catch (...)
    {
        terminate_allocation(a_context);
    }
}
} // namespace

namespace cue
{
SystemCapabilityQueryReport query_windows_system_capabilities(const AssertContext &a_assertContext) noexcept
{
    LogResult diagnosticResult = LogResult::Success;
    SYSTEM_INFO processInfo = {};
    SYSTEM_INFO nativeInfo = {};
    GetSystemInfo(&processInfo);
    GetNativeSystemInfo(&nativeInfo);

    SystemCapabilityValue<std::uint64_t> physicalMemory = SystemCapabilityValue<std::uint64_t>::query_failed();
    MEMORYSTATUSEX memoryStatus = {};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus) != FALSE)
    {
        physicalMemory = SystemCapabilityValue<std::uint64_t>::known(memoryStatus.ullTotalPhys);
    }
    else
    {
        log_query_failure(a_assertContext, k_memoryQueryFailed, "Windows physical memory query failed", GetLastError(),
                          diagnosticResult);
    }

    SystemCapabilitySnapshotDescription description = {
        map_architecture(processInfo.wProcessorArchitecture),
        map_architecture(nativeInfo.wProcessorArchitecture),
        query_logical_processor_count(a_assertContext, diagnosticResult),
        physicalMemory,
        SystemCapabilityValue<std::uint32_t>::known(processInfo.dwPageSize),
        query_cache_line_size(a_assertContext, diagnosticResult),
        windows_private::map_system_instruction_capabilities(query_cpu_capabilities()),
    };
    return {SystemCapabilitySnapshot::create(description), diagnosticResult};
}
} // namespace cue
