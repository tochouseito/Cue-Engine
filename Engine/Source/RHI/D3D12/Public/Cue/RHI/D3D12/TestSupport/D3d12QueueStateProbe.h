#pragma once

#include <Cue/Foundation/Result.h>

#include <cstdint>

namespace cue
{
class AssertContext;

struct D3d12QueueStateProbeReport final
{
    std::uint64_t reservedFenceValue;
    std::uint64_t completedBeforeSignal;
    std::uint64_t completedAfterWait;
    std::uint64_t lastSignaledFence;
    bool usedEventWaitPath;
};

enum class D3d12QueueFaultProbeMode
{
    WaitRecoveryMatrix,
    WaitSentinelRefreshRemoval,
    StaleEventFollowup,
    WaitFailedUnavailable,
    EventCloseFailure,
    EventCreateFailure,
    SignalCompletion,
    SignalDeviceRemoved,
    SignalCompletionDeviceRemovedRace,
    SignalUnavailable,
    SignalEventCloseFailure,
    SignalEventCreateFailure,
    TerminalSignalCompletion,
    TerminalSignalDeviceRemoved,
    TerminalSignalUnavailable,
    TerminalCloseFailure,
};

/// @brief WARP DeviceでDirect Queue、Fence、EventのSignal／Wait／Shutdownを検証する
[[nodiscard]] Result<D3d12QueueStateProbeReport> probe_d3d12_queue_state(const AssertContext &a_assertContext) noexcept;

/// @brief Fence値上限で最後のSignalと非Wrapの枯渇処理を検証する
[[nodiscard]] bool verify_d3d12_fence_exhaustion_for_probe(const AssertContext &a_assertContext) noexcept;

/// @brief Native境界の異常注入でQueue/Fenceの判定行列と所有権を検証する
[[nodiscard]] bool verify_d3d12_queue_fault_for_probe(D3d12QueueFaultProbeMode a_mode,
                                                      const AssertContext &a_assertContext) noexcept;
} // namespace cue
