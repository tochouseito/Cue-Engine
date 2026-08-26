#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12FrameCommandProbe.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class ProcessFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief 回復不能な失敗の終了要求を処理し、実装が定める Process 終了動作を実行する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(90);
    }

    /// @brief 回復不能な失敗の終了要求を処理し、実装が定める Process 終了動作を実行する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(91);
    }
};

class ProcessLogSink final : public cue::LogSink
{
  public:
    /// @brief 受け取った Log Record を対象 Sink へ書き込み、出力成否を返す
    [[nodiscard]] bool write(const cue::LogRecord &) override
    {
        return true;
    }

    /// @brief 対象 Sink に保留中の Log 出力を反映し、完了成否を返す
    [[nodiscard]] bool flush() override
    {
        return true;
    }
};
} // namespace

/// @brief 指定 Scenario で Frame Command 処理を実行し、Lifecycle と失敗経路を Process 単位で検証する
int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 1;
    }

    ProcessFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    sinks.push_back(std::make_unique<ProcessLogSink>());
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    const std::string_view mode = a_arguments[1];

    if (mode == "Smoke300")
    {
        cue::Result<cue::D3d12FrameCommandProbeReport> result = cue::probe_d3d12_frame_commands(assertContext);
        const cue::D3d12FrameCommandProbeReport *report = result.try_value();
        return report != nullptr && report->iterationCount == 300 && report->frameZeroResetCount == 150 &&
                       report->frameOneResetCount == 150 && report->executeCount == 300 &&
                       report->barrierCount == 600 && report->barriersAreValid &&
                       report->backBuffersReturnedToPresent &&
                       report->lastSubmittedFence == 300 && report->infoQueueErrorCount == 0 &&
                       report->frameNamesContainIndices && report->fenceCheckedBeforeAllocatorReuse
                   ? 0
                   : 2;
    }

    if (mode == "InfoQueue300")
    {
        cue::Result<cue::D3d12FrameCommandProbeReport> result = cue::probe_d3d12_frame_commands(assertContext);
        const cue::D3d12FrameCommandProbeReport *report = result.try_value();

        if (report == nullptr)
        {
            return 18;
        }

        if (!report->diagnosticsAvailable)
        {
            return 77;
        }

        return report->iterationCount == 300 && report->executeCount == 300 && report->infoQueueErrorCount == 0 ? 0
                                                                                                                : 19;
    }

    if (mode == "InvalidIndex")
    {
        return cue::verify_d3d12_invalid_frame_index_for_probe(assertContext) ? 0 : 3;
    }

    if (mode == "ClearTwoColors")
    {
        return cue::verify_d3d12_back_buffer_clear_for_probe(assertContext) ? 0 : 28;
    }

    if (mode == "ClearOutsideRecording")
    {
        return cue::verify_d3d12_back_buffer_clear_rejection_for_probe(
                   cue::D3d12BackBufferClearRejectionProbeMode::OutsideRecording, assertContext)
                   ? 0
                   : 29;
    }

    if (mode == "ClearNonCurrentFrame")
    {
        return cue::verify_d3d12_back_buffer_clear_rejection_for_probe(
                   cue::D3d12BackBufferClearRejectionProbeMode::NonCurrentFrame, assertContext)
                   ? 0
                   : 30;
    }

    if (mode == "ClearPresentState")
    {
        return cue::verify_d3d12_back_buffer_clear_rejection_for_probe(
                   cue::D3d12BackBufferClearRejectionProbeMode::PresentState, assertContext)
                   ? 0
                   : 31;
    }

    if (mode == "ClearMissingRtv")
    {
        return cue::verify_d3d12_back_buffer_clear_rejection_for_probe(
                   cue::D3d12BackBufferClearRejectionProbeMode::MissingRtv, assertContext)
                   ? 0
                   : 32;
    }

    if (mode == "ClearInvalidRtvHandle")
    {
        return cue::verify_d3d12_back_buffer_clear_rejection_for_probe(
                   cue::D3d12BackBufferClearRejectionProbeMode::InvalidRtvHandle, assertContext)
                   ? 0
                   : 33;
    }

    if (mode == "TransitionInvalidIndex")
    {
        return cue::verify_d3d12_transition_invalid_index_for_probe(assertContext) ? 0 : 23;
    }

    if (mode == "TransitionNullResource")
    {
        return cue::verify_d3d12_transition_null_resource_for_probe(assertContext) ? 0 : 24;
    }

    if (mode == "TransitionOutsideRecording")
    {
        return cue::verify_d3d12_transition_order_for_probe(
                   cue::D3d12BackBufferTransitionOrderProbeMode::OutsideRecording, assertContext)
                   ? 0
                   : 25;
    }

    if (mode == "TransitionNonCurrentFrame")
    {
        return cue::verify_d3d12_transition_order_for_probe(
                   cue::D3d12BackBufferTransitionOrderProbeMode::NonCurrentFrame, assertContext)
                   ? 0
                   : 26;
    }

    if (mode == "TransitionUnknownTarget")
    {
        return cue::verify_d3d12_transition_unknown_target_for_probe(assertContext) ? 0 : 27;
    }

    if (mode == "WaitRecoveryMatrix")
    {
        return cue::verify_d3d12_frame_wait_recovery_for_probe(assertContext) ? 0 : 12;
    }

    if (mode == "BeginFrameTerminalOutcomeMatrix")
    {
        return cue::verify_d3d12_begin_frame_terminal_outcome_matrix_for_probe(assertContext) ? 0 : 14;
    }

    if (mode == "FrameResetFailedTerminalMatrix")
    {
        return cue::verify_d3d12_frame_reset_failed_terminal_matrix_for_probe(assertContext) ? 0 : 15;
    }

    if (mode == "RecordingCloseFailedTerminalMatrix")
    {
        return cue::verify_d3d12_recording_close_failed_terminal_matrix_for_probe(assertContext) ? 0 : 16;
    }

    if (mode == "FenceExhaustionMatrix")
    {
        return cue::verify_d3d12_frame_fence_exhaustion_matrix_for_probe(assertContext) ? 0 : 17;
    }

    if (mode == "FrameSignalOutcomeMatrix")
    {
        return cue::verify_d3d12_frame_signal_outcome_matrix_for_probe(assertContext) ? 0 : 20;
    }

    if (mode == "StateOrderBegin")
    {
        return cue::verify_d3d12_frame_state_order_for_probe(cue::D3d12FrameCommandOrderProbeMode::Begin, assertContext)
                   ? 0
                   : 4;
    }

    if (mode == "StateOrderClose")
    {
        return cue::verify_d3d12_frame_state_order_for_probe(cue::D3d12FrameCommandOrderProbeMode::Close, assertContext)
                   ? 0
                   : 9;
    }

    if (mode == "StateOrderExecute")
    {
        return cue::verify_d3d12_frame_state_order_for_probe(cue::D3d12FrameCommandOrderProbeMode::Execute,
                                                             assertContext)
                   ? 0
                   : 10;
    }

    if (mode == "StateOrderPresentState")
    {
        return cue::verify_d3d12_frame_state_order_for_probe(
                   cue::D3d12FrameCommandOrderProbeMode::PresentState, assertContext)
                   ? 0
                   : 26;
    }

    if (mode == "StateOrderResizeSuspend")
    {
        return cue::verify_d3d12_frame_state_order_for_probe(
                   cue::D3d12FrameCommandOrderProbeMode::ResizeSuspend, assertContext)
                   ? 0
                   : 22;
    }

    if (mode == "AllocatorResetFailure")
    {
        return cue::verify_d3d12_frame_command_fault_for_probe(cue::D3d12FrameCommandFaultProbeMode::AllocatorReset,
                                                               assertContext)
                   ? 0
                   : 5;
    }

    if (mode == "CommandListResetFailure")
    {
        return cue::verify_d3d12_frame_command_fault_for_probe(cue::D3d12FrameCommandFaultProbeMode::CommandListReset,
                                                               assertContext)
                   ? 0
                   : 6;
    }

    if (mode == "CommandListCloseFailure")
    {
        return cue::verify_d3d12_frame_command_fault_for_probe(cue::D3d12FrameCommandFaultProbeMode::CommandListClose,
                                                               assertContext)
                   ? 0
                   : 7;
    }

    if (mode == "ResizePreparationFailureMatrix")
    {
        return cue::verify_d3d12_resize_preparation_fault_matrix_for_probe(assertContext) ? 0 : 21;
    }

    return 13;
}
