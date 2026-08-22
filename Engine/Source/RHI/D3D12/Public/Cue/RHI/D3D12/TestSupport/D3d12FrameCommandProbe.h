#pragma once

#include <Cue/Foundation/Result.h>

#include <cstdint>

namespace cue
{
class AssertContext;

struct D3d12FrameCommandProbeReport final
{
    std::uint32_t iterationCount;
    std::uint32_t frameZeroResetCount;
    std::uint32_t frameOneResetCount;
    std::uint32_t executeCount;
    std::uint64_t lastSubmittedFence;
    std::uint64_t infoQueueErrorCount;
    bool frameNamesContainIndices;
    bool fenceCheckedBeforeAllocatorReuse;
    bool diagnosticsAvailable;
};

enum class D3d12FrameCommandFaultProbeMode
{
    AllocatorReset,
    CommandListReset,
    CommandListClose,
};

enum class D3d12FrameCommandOrderProbeMode
{
    Begin,
    Close,
    Execute,
};

/** @brief WARP上で2 Frame Contextを300回周回して空Command ListのLifecycleを検証する */
[[nodiscard]] Result<D3d12FrameCommandProbeReport> probe_d3d12_frame_commands(
    const AssertContext &a_assertContext) noexcept;

/** @brief Frame Index範囲外がNative操作前にErrorになることを検証する */
[[nodiscard]] bool verify_d3d12_invalid_frame_index_for_probe(const AssertContext &a_assertContext) noexcept;

/** @brief Frame再利用Wait異常後にResetを延期し、Event復旧後の再試行だけを許可することを検証する */
[[nodiscard]] bool verify_d3d12_frame_wait_recovery_for_probe(const AssertContext &a_assertContext) noexcept;

/** @brief Frame再利用WaitのEvent復旧失敗、完了不能、Device Removal、stale Event追試を検証する */
[[nodiscard]] bool verify_d3d12_begin_frame_terminal_outcome_matrix_for_probe(
    const AssertContext &a_assertContext) noexcept;

/** @brief FrameResetFailedからのterminal Signal／Wait結果とResource保持規則を検証する */
[[nodiscard]] bool verify_d3d12_frame_reset_failed_terminal_matrix_for_probe(
    const AssertContext &a_assertContext) noexcept;

/** @brief RecordingCloseFailedからのterminal Signal／Wait結果とResource保持規則を検証する */
[[nodiscard]] bool verify_d3d12_recording_close_failed_terminal_matrix_for_probe(
    const AssertContext &a_assertContext) noexcept;

/** @brief Fence枯渇時にExecuteせず明示Discardし、後発Errorを保持することを検証する */
[[nodiscard]] bool verify_d3d12_frame_fence_exhaustion_matrix_for_probe(const AssertContext &a_assertContext) noexcept;

/** @brief 通常Frame Signal失敗後の完了、Unavailable、Device Removal分岐を検証する */
[[nodiscard]] bool verify_d3d12_frame_signal_outcome_matrix_for_probe(const AssertContext &a_assertContext) noexcept;

/** @brief Command Listの順序違反をDebug AssertまたはRelease Errorとして検証する */
[[nodiscard]] bool verify_d3d12_frame_state_order_for_probe(D3d12FrameCommandOrderProbeMode a_mode,
                                                            const AssertContext &a_assertContext) noexcept;

/** @brief Reset／Close失敗後の状態と未Executeを検証する */
[[nodiscard]] bool verify_d3d12_frame_command_fault_for_probe(D3d12FrameCommandFaultProbeMode a_mode,
                                                              const AssertContext &a_assertContext) noexcept;
} // namespace cue
