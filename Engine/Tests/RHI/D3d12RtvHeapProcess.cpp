#include "TestSupport/RhiProcessTestFixture.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12RtvHeapProbe.h>

#include <string_view>

/// @brief 指定 Scenario で RTV Heap 処理を実行し、Descriptor 所有権と失敗経路を Process 単位で検証する
int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 2)
    {
        return 1;
    }

    cue::test::RhiProcessTestFixture fixture;
    cue::AssertContext &assertContext = fixture.assert_context();
    const std::string_view mode = a_arguments[1];

    if (mode == "Smoke")
    {
        cue::Result<cue::D3d12RtvHeapProbeReport> result = cue::probe_d3d12_rtv_heap(assertContext);
        const cue::D3d12RtvHeapProbeReport *report = result.try_value();
        return report != nullptr && report->capacity == 2 && report->usedAtCapacity == 2 &&
                       report->firstSlotIndex == 0 && report->secondSlotIndex == 1 && report->reusedSlotIndex == 0 &&
                       report->descriptorIncrementSize > 0 && report->infoQueueErrorCount == 0 &&
                       report->descriptorShapeIsValid && report->cpuHandlesAreValid && report->capacityErrorDetected &&
                       report->generationAdvancedOnReuse
                   ? 0
                   : 2;
    }

    if (mode == "InfoQueue")
    {
        cue::Result<cue::D3d12RtvHeapProbeReport> result = cue::probe_d3d12_rtv_heap(assertContext);
        const cue::D3d12RtvHeapProbeReport *report = result.try_value();

        if (report == nullptr)
        {
            return 3;
        }

        if (!report->diagnosticsAvailable)
        {
            return 77;
        }

        return report->infoQueueErrorCount == 0 ? 0 : 4;
    }

    if (mode == "CreationFaults")
    {
        return cue::verify_d3d12_rtv_heap_creation_faults_for_probe(assertContext) ? 0 : 5;
    }

    if (mode == "HandleOverflow")
    {
        return cue::verify_d3d12_rtv_heap_handle_overflow_for_probe(assertContext) ? 0 : 6;
    }

    if (mode == "GenerationExhaustion")
    {
        return cue::verify_d3d12_rtv_heap_generation_exhaustion_for_probe(assertContext) ? 0 : 11;
    }

    if (mode == "HeapMove")
    {
        return cue::verify_d3d12_rtv_heap_move_for_probe(assertContext) ? 0 : 12;
    }

    if (mode == "DoubleRelease")
    {
        return cue::verify_d3d12_rtv_heap_violation_for_probe(cue::D3d12RtvHeapViolationProbeMode::DoubleRelease,
                                                              assertContext)
                   ? 0
                   : 7;
    }

    if (mode == "StaleRelease")
    {
        return cue::verify_d3d12_rtv_heap_violation_for_probe(cue::D3d12RtvHeapViolationProbeMode::StaleRelease,
                                                              assertContext)
                   ? 0
                   : 8;
    }

    if (mode == "ForeignRelease")
    {
        return cue::verify_d3d12_rtv_heap_violation_for_probe(cue::D3d12RtvHeapViolationProbeMode::ForeignRelease,
                                                              assertContext)
                   ? 0
                   : 13;
    }

    if (mode == "LiveSlotShutdown")
    {
        return cue::verify_d3d12_rtv_heap_violation_for_probe(cue::D3d12RtvHeapViolationProbeMode::LiveSlotShutdown,
                                                              assertContext)
                   ? 0
                   : 9;
    }

    if (mode == "OwnerDestructor")
    {
        return cue::trigger_d3d12_rtv_heap_owner_violation_for_probe(
                   cue::D3d12RtvHeapOwnerViolationProbeMode::DestructorBeforeShutdown, assertContext)
                   ? 0
                   : 14;
    }

    if (mode == "ActiveMoveAssignment")
    {
        return cue::trigger_d3d12_rtv_heap_owner_violation_for_probe(
                   cue::D3d12RtvHeapOwnerViolationProbeMode::ActiveMoveAssignment, assertContext)
                   ? 0
                   : 15;
    }

    return 10;
}
