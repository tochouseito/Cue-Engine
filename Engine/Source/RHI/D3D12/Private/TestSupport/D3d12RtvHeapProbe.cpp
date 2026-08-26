#include <Cue/RHI/D3D12/TestSupport/D3d12RtvHeapProbe.h>

#include "D3d12AdapterSelection.h"
#include "D3d12DeviceCreation.h"
#include "D3d12Diagnostics.h"
#include "D3d12RtvHeap.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>

#include <d3d12sdklayers.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace
{
enum class ProbeCreationFault
{
    None,
    Creation,
    Name,
    ZeroIncrement,
    NullStart,
    OverflowStart,
};

struct ProbeNativeState final
{
    ProbeCreationFault fault = ProbeCreationFault::None;
    D3D12_DESCRIPTOR_HEAP_DESC descriptor = {};
    D3D12_CPU_DESCRIPTOR_HANDLE startHandle = {};
    bool descriptorCaptured = false;
    bool failureHandlerCalled = false;
    bool failureResourceWasAlive = false;
};

struct ProbeDevice final
{
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    cue::D3d12DiagnosticsStatus diagnostics;
};

thread_local ProbeNativeState g_probeState;

/// @brief Probe 間で状態が混ざらないよう Fault Injection 用 Global 状態を初期化する
void reset_probe_state(ProbeCreationFault a_fault) noexcept
{
    g_probeState = {};
    g_probeState.fault = a_fault;
}

/// @brief D3D12 RTV Heap Probe で使用する Heap For Probe を生成し、呼び出し元へ返す
HRESULT create_heap_for_probe(ID3D12Device *a_device, const D3D12_DESCRIPTOR_HEAP_DESC *a_descriptor,
                              ID3D12DescriptorHeap **a_heap) noexcept
{
    g_probeState.descriptor = *a_descriptor;
    g_probeState.descriptorCaptured = true;

    if (g_probeState.fault == ProbeCreationFault::Creation)
    {
        return E_FAIL;
    }

    return a_device->CreateDescriptorHeap(a_descriptor, IID_PPV_ARGS(a_heap));
}

/// @brief D3D12 RTV Heap Probe が保持する Get Increment For Probe を呼び出し元へ返す
UINT get_increment_for_probe(ID3D12Device *a_device) noexcept
{
    if (g_probeState.fault == ProbeCreationFault::ZeroIncrement)
    {
        return 0;
    }

    return a_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

/// @brief D3D12 RTV Heap Probe が保持する Get Start For Probe を呼び出し元へ返す
D3D12_CPU_DESCRIPTOR_HANDLE get_start_for_probe(ID3D12DescriptorHeap *a_heap) noexcept
{
    if (g_probeState.fault == ProbeCreationFault::OverflowStart)
    {
        g_probeState.startHandle = {(std::numeric_limits<SIZE_T>::max)()};
        return g_probeState.startHandle;
    }

    if (g_probeState.fault == ProbeCreationFault::NullStart)
    {
        g_probeState.startHandle = {};
        return g_probeState.startHandle;
    }

    g_probeState.startHandle = a_heap->GetCPUDescriptorHandleForHeapStart();
    return g_probeState.startHandle;
}

/// @brief D3D12 RTV Heap Probe の Name For Probe を整合性を保って更新する
HRESULT set_name_for_probe(ID3D12Object *a_object, LPCWSTR a_name) noexcept
{
    if (g_probeState.fault == ProbeCreationFault::Name)
    {
        return E_FAIL;
    }

    return a_object->SetName(a_name);
}

/// @brief D3D12 RTV Heap Probe で使用する Probe Functions を生成し、呼び出し元へ返す
[[nodiscard]] cue::D3d12RtvHeapNativeFunctions make_probe_functions() noexcept
{
    return {
        create_heap_for_probe,
        get_increment_for_probe,
        get_start_for_probe,
        set_name_for_probe,
    };
}

/// @brief D3D12 RTV Heap Probe の Native Failure For Probe を規定された順序と失敗規則で処理する
cue::Result<void> handle_native_failure_for_probe(void *, cue::Error &&a_error,
                                                  const cue::D3d12RtvHeapFailureResources &a_resources) noexcept
{
    g_probeState.failureHandlerCalled = true;
    g_probeState.failureResourceWasAlive =
        g_probeState.fault == ProbeCreationFault::Creation ? a_resources.heap == nullptr : a_resources.heap != nullptr;
    return cue::Result<void>::failure(std::move(a_error));
}

/// @brief D3D12 RTV Heap Probe で使用する Probe Device を生成し、呼び出し元へ返す
[[nodiscard]] cue::Result<ProbeDevice> create_probe_device(bool a_enableDiagnostics,
                                                           const cue::AssertContext &a_assertContext) noexcept
{
    cue::D3d12BackendDescriptor descriptor = {};
    descriptor.adapterPolicy = cue::D3d12AdapterPolicy::Warp;
    descriptor.validationMode = a_enableDiagnostics && cue::are_d3d12_diagnostics_allowed()
                                    ? cue::D3d12ValidationMode::Standard
                                    : cue::D3d12ValidationMode::Disabled;
    descriptor.isDredEnabled = false;
    descriptor.gpuWaitTimeoutMilliseconds = 5'000;
    cue::Result<cue::D3d12DiagnosticsStatus> diagnosticsResult =
        cue::configure_d3d12_pre_device_diagnostics(descriptor, a_assertContext);

    if (!diagnosticsResult)
    {
        return cue::Result<ProbeDevice>::failure(std::move(*diagnosticsResult.try_error()));
    }

    cue::D3d12DiagnosticsStatus diagnostics = *diagnosticsResult.try_value();
    cue::Result<cue::D3d12AdapterSelection> selectionResult =
        cue::select_d3d12_adapter(cue::D3d12AdapterPolicy::Warp, diagnostics, a_assertContext);

    if (!selectionResult)
    {
        return cue::Result<ProbeDevice>::failure(std::move(*selectionResult.try_error()));
    }

    cue::D3d12AdapterSelection selection = std::move(*selectionResult.try_value());
    cue::Result<Microsoft::WRL::ComPtr<ID3D12Device>> deviceResult =
        cue::create_d3d12_device(selection.adapter.Get(), selection.featureLevel, a_assertContext);

    if (!deviceResult)
    {
        return cue::Result<ProbeDevice>::failure(std::move(*deviceResult.try_error()));
    }

    ProbeDevice probeDevice = {std::move(*deviceResult.try_value()), diagnostics};
    cue::Result<void> infoQueueResult =
        cue::configure_d3d12_info_queue(probeDevice.device.Get(), probeDevice.diagnostics, a_assertContext);

    if (!infoQueueResult)
    {
        return cue::Result<ProbeDevice>::failure(std::move(*infoQueueResult.try_error()));
    }

    return cue::Result<ProbeDevice>::success(std::move(probeDevice));
}

/// @brief D3D12 Info Queue に記録された Error Severity 以上の Message 数を返す
[[nodiscard]] std::uint64_t count_info_queue_errors(ID3D12Device *a_device) noexcept
{
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;

    if (FAILED(a_device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
    {
        return 0;
    }

    const std::uint64_t messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    std::uint64_t errorCount = 0;

    for (std::uint64_t messageIndex = 0; messageIndex < messageCount; ++messageIndex)
    {
        SIZE_T messageSize = 0;

        if (FAILED(infoQueue->GetMessage(messageIndex, nullptr, &messageSize)) || messageSize == 0)
        {
            ++errorCount;
            continue;
        }

        try
        {
            std::vector<std::byte> storage(messageSize);
            D3D12_MESSAGE *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());

            if (FAILED(infoQueue->GetMessage(messageIndex, message, &messageSize)) ||
                message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            {
                ++errorCount;
            }
        }
        catch (...)
        {
            return errorCount + 1;
        }
    }

    return errorCount;
}

/// @brief Error が指定された Domain と Code を保持しているかを判定する
[[nodiscard]] bool has_error_code(const cue::Error *a_error, std::int64_t a_value) noexcept
{
    return a_error != nullptr && a_error->code().domain() == "Cue.RHI.D3D12" && a_error->code().value() == a_value;
}

/// @brief D3D12 RTV Heap Probe の Heap を依存関係と完了条件を守って安全に解放または停止する
[[nodiscard]] bool finish_heap(cue::D3d12RtvHeap &a_heap) noexcept
{
    cue::Result<void> shutdownResult = a_heap.shutdown();
    return static_cast<bool>(shutdownResult);
}
} // namespace

namespace cue
{
Result<D3d12RtvHeapProbeReport> probe_d3d12_rtv_heap(const AssertContext &a_assertContext) noexcept
{
    reset_probe_state(ProbeCreationFault::None);
    Result<ProbeDevice> deviceResult = create_probe_device(true, a_assertContext);

    if (!deviceResult)
    {
        return Result<D3d12RtvHeapProbeReport>::failure(std::move(*deviceResult.try_error()));
    }

    ProbeDevice probeDevice = std::move(*deviceResult.try_value());
    D3d12RtvHeapNativeFunctions functions = make_probe_functions();
    Result<D3d12RtvHeap> heapResult = create_d3d12_rtv_heap(probeDevice.device.Get(), a_assertContext, functions);

    if (!heapResult)
    {
        return Result<D3d12RtvHeapProbeReport>::failure(std::move(*heapResult.try_error()));
    }

    D3d12RtvHeap heap = std::move(*heapResult.try_value());
    Result<D3d12RtvSlot> firstResult = heap.allocate();
    Result<D3d12RtvSlot> secondResult = heap.allocate();

    if (!firstResult || !secondResult)
    {
        if (firstResult)
        {
            static_cast<void>(heap.release(*firstResult.try_value()));
        }

        if (secondResult)
        {
            static_cast<void>(heap.release(*secondResult.try_value()));
        }

        static_cast<void>(heap.shutdown());
        Error error = firstResult ? std::move(*secondResult.try_error()) : std::move(*firstResult.try_error());
        return Result<D3d12RtvHeapProbeReport>::failure(std::move(error));
    }

    const D3d12RtvSlot first = *firstResult.try_value();
    const D3d12RtvSlot second = *secondResult.try_value();
    Result<D3D12_CPU_DESCRIPTOR_HANDLE> firstHandleResult = heap.cpu_handle(first);
    Result<D3D12_CPU_DESCRIPTOR_HANDLE> secondHandleResult = heap.cpu_handle(second);
    Result<D3d12RtvSlot> capacityResult = heap.allocate();
    const bool capacityErrorDetected = has_error_code(capacityResult.try_error(), 68);
    const std::uint32_t usedAtCapacity = heap.used_count();
    Result<void> firstReleaseResult = heap.release(first);

    if (!firstReleaseResult)
    {
        static_cast<void>(heap.release(second));
        static_cast<void>(heap.shutdown());
        return Result<D3d12RtvHeapProbeReport>::failure(std::move(*firstReleaseResult.try_error()));
    }

    Result<D3d12RtvSlot> reusedResult = heap.allocate();

    if (!reusedResult)
    {
        static_cast<void>(heap.release(second));
        static_cast<void>(heap.shutdown());
        return Result<D3d12RtvHeapProbeReport>::failure(std::move(*reusedResult.try_error()));
    }

    const D3d12RtvSlot reused = *reusedResult.try_value();
    const bool handlesValid =
        firstHandleResult && secondHandleResult && firstHandleResult.try_value()->ptr == g_probeState.startHandle.ptr &&
        secondHandleResult.try_value()->ptr == g_probeState.startHandle.ptr + heap.descriptor_increment_size();
    const bool descriptorShapeValid =
        g_probeState.descriptorCaptured && g_probeState.descriptor.Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV &&
        g_probeState.descriptor.NumDescriptors == k_d3d12RtvDescriptorCapacity &&
        g_probeState.descriptor.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_NONE && g_probeState.descriptor.NodeMask == 0;
    const std::uint32_t incrementSize = heap.descriptor_increment_size();
    const std::uint32_t capacity = heap.capacity();
    const bool generationAdvanced = reused.index == first.index && reused.generation != first.generation;
    Result<void> secondReleaseResult = heap.release(second);
    Result<void> reusedReleaseResult = heap.release(reused);
    Result<void> shutdownResult = heap.shutdown();

    if (!secondReleaseResult || !reusedReleaseResult || !shutdownResult)
    {
        Error error = !secondReleaseResult   ? std::move(*secondReleaseResult.try_error())
                      : !reusedReleaseResult ? std::move(*reusedReleaseResult.try_error())
                                             : std::move(*shutdownResult.try_error());
        return Result<D3d12RtvHeapProbeReport>::failure(std::move(error));
    }

    Result<void> liveObjectResult =
        report_d3d12_live_device_objects(probeDevice.device.Get(), probeDevice.diagnostics, a_assertContext);

    if (!liveObjectResult)
    {
        return Result<D3d12RtvHeapProbeReport>::failure(std::move(*liveObjectResult.try_error()));
    }

    D3d12RtvHeapProbeReport report = {};
    report.capacity = capacity;
    report.usedAtCapacity = usedAtCapacity;
    report.firstSlotIndex = first.index;
    report.secondSlotIndex = second.index;
    report.reusedSlotIndex = reused.index;
    report.descriptorIncrementSize = incrementSize;
    report.infoQueueErrorCount = count_info_queue_errors(probeDevice.device.Get());
    report.descriptorShapeIsValid = descriptorShapeValid;
    report.cpuHandlesAreValid = handlesValid;
    report.capacityErrorDetected = capacityErrorDetected;
    report.generationAdvancedOnReuse = generationAdvanced;
    report.diagnosticsAvailable = probeDevice.diagnostics.isInfoQueueEnabled;
    return Result<D3d12RtvHeapProbeReport>::success(std::move(report));
}

bool verify_d3d12_rtv_heap_creation_faults_for_probe(const AssertContext &a_assertContext) noexcept
{
    Result<ProbeDevice> deviceResult = create_probe_device(false, a_assertContext);

    if (!deviceResult)
    {
        return false;
    }

    ProbeDevice device = std::move(*deviceResult.try_value());
    constexpr ProbeCreationFault faults[] = {
        ProbeCreationFault::Creation,
        ProbeCreationFault::Name,
        ProbeCreationFault::ZeroIncrement,
        ProbeCreationFault::NullStart,
    };
    constexpr std::int64_t expectedCodes[] = {65, 66, 67, 67};

    for (std::size_t index = 0; index < std::size(faults); ++index)
    {
        reset_probe_state(faults[index]);
        D3d12RtvHeapNativeFunctions functions = make_probe_functions();
        D3d12RtvHeapFailureHandler failureHandler = {
            nullptr,
            handle_native_failure_for_probe,
        };
        Result<D3d12RtvHeap> result =
            create_d3d12_rtv_heap(device.device.Get(), a_assertContext, functions, failureHandler);
        const bool isNativeFault =
            faults[index] == ProbeCreationFault::Creation || faults[index] == ProbeCreationFault::Name;
        const bool failureHandlerValid = isNativeFault
                                             ? g_probeState.failureHandlerCalled && g_probeState.failureResourceWasAlive
                                             : !g_probeState.failureHandlerCalled;

        if (result || !has_error_code(result.try_error(), expectedCodes[index]) || !failureHandlerValid)
        {
            if (result)
            {
                static_cast<void>(result.try_value()->shutdown());
            }

            return false;
        }
    }

    return true;
}

bool verify_d3d12_rtv_heap_handle_overflow_for_probe(const AssertContext &a_assertContext) noexcept
{
    reset_probe_state(ProbeCreationFault::OverflowStart);
    Result<ProbeDevice> deviceResult = create_probe_device(false, a_assertContext);

    if (!deviceResult)
    {
        return false;
    }

    ProbeDevice device = std::move(*deviceResult.try_value());
    D3d12RtvHeapNativeFunctions functions = make_probe_functions();
    Result<D3d12RtvHeap> heapResult = create_d3d12_rtv_heap(device.device.Get(), a_assertContext, functions);
    return has_error_code(heapResult.try_error(), 71);
}

bool verify_d3d12_rtv_heap_generation_exhaustion_for_probe(const AssertContext &a_assertContext) noexcept
{
    reset_probe_state(ProbeCreationFault::None);
    Result<ProbeDevice> deviceResult = create_probe_device(false, a_assertContext);

    if (!deviceResult)
    {
        return false;
    }

    ProbeDevice device = std::move(*deviceResult.try_value());
    D3d12RtvHeapNativeFunctions functions = make_probe_functions();
    Result<D3d12RtvHeap> heapResult = create_d3d12_rtv_heap(device.device.Get(), a_assertContext, functions);

    if (!heapResult)
    {
        return false;
    }

    D3d12RtvHeap heap = std::move(*heapResult.try_value());
    const std::uint64_t maximumGeneration = (std::numeric_limits<std::uint64_t>::max)();
    heap.set_slot_generation_for_test(0, maximumGeneration);
    heap.set_slot_generation_for_test(1, maximumGeneration);
    Result<D3d12RtvSlot> allocationResult = heap.allocate();
    const bool exhausted = has_error_code(allocationResult.try_error(), 76) && heap.used_count() == 0;
    return finish_heap(heap) && exhausted;
}

bool verify_d3d12_rtv_heap_move_for_probe(const AssertContext &a_assertContext) noexcept
{
    reset_probe_state(ProbeCreationFault::None);
    Result<ProbeDevice> deviceResult = create_probe_device(false, a_assertContext);

    if (!deviceResult)
    {
        return false;
    }

    ProbeDevice device = std::move(*deviceResult.try_value());
    D3d12RtvHeapNativeFunctions functions = make_probe_functions();
    Result<D3d12RtvHeap> heapResult = create_d3d12_rtv_heap(device.device.Get(), a_assertContext, functions);

    if (!heapResult)
    {
        return false;
    }

    D3d12RtvHeap source = std::move(*heapResult.try_value());
    Result<D3d12RtvSlot> slotResult = source.allocate();

    if (!slotResult)
    {
        static_cast<void>(source.shutdown());
        return false;
    }

    const D3d12RtvSlot slot = *slotResult.try_value();
    D3d12RtvHeap target(std::move(source));
    Result<D3d12RtvSlot> movedFromAllocation = source.allocate();
    const bool movedFromIsDiagnosable = has_error_code(movedFromAllocation.try_error(), 74);
    const bool handleValid = static_cast<bool>(target.cpu_handle(slot));
    const bool released = static_cast<bool>(target.release(slot));
    return finish_heap(target) && handleValid && released && movedFromIsDiagnosable && !source.has_native_object();
}

bool verify_d3d12_rtv_heap_violation_for_probe(D3d12RtvHeapViolationProbeMode a_mode,
                                               const AssertContext &a_assertContext) noexcept
{
    reset_probe_state(ProbeCreationFault::None);
    Result<ProbeDevice> deviceResult = create_probe_device(false, a_assertContext);

    if (!deviceResult)
    {
        return false;
    }

    ProbeDevice device = std::move(*deviceResult.try_value());
    D3d12RtvHeapNativeFunctions functions = make_probe_functions();
    Result<D3d12RtvHeap> heapResult = create_d3d12_rtv_heap(device.device.Get(), a_assertContext, functions);

    if (!heapResult)
    {
        return false;
    }

    D3d12RtvHeap heap = std::move(*heapResult.try_value());
    Result<D3d12RtvSlot> slotResult = heap.allocate();

    if (!slotResult)
    {
        return false;
    }

    const D3d12RtvSlot original = *slotResult.try_value();

    if (a_mode == D3d12RtvHeapViolationProbeMode::ForeignRelease)
    {
        const bool originalReleased = static_cast<bool>(heap.release(original));
        const bool originalShutdown = finish_heap(heap);
        Result<D3d12RtvHeap> replacementResult = create_d3d12_rtv_heap(device.device.Get(), a_assertContext, functions);

        if (!originalReleased || !originalShutdown || !replacementResult)
        {
            return false;
        }

        D3d12RtvHeap replacement = std::move(*replacementResult.try_value());
        Result<D3d12RtvSlot> replacementSlotResult = replacement.allocate();

        if (!replacementSlotResult)
        {
            static_cast<void>(replacement.shutdown());
            return false;
        }

        Result<void> foreignResult = replacement.release(original);
        const bool detected = has_error_code(foreignResult.try_error(), 70);
        const bool replacementReleased = static_cast<bool>(replacement.release(*replacementSlotResult.try_value()));
        return finish_heap(replacement) && detected && replacementReleased;
    }

    if (a_mode == D3d12RtvHeapViolationProbeMode::LiveSlotShutdown)
    {
        Result<void> invalidResult = heap.shutdown();
        const bool detected = has_error_code(invalidResult.try_error(), 72);
        const bool released = static_cast<bool>(heap.release(original));
        return finish_heap(heap) && detected && released;
    }

    if (!heap.release(original))
    {
        return false;
    }

    if (a_mode == D3d12RtvHeapViolationProbeMode::DoubleRelease)
    {
        Result<void> invalidResult = heap.release(original);
        return finish_heap(heap) && has_error_code(invalidResult.try_error(), 70);
    }

    Result<D3d12RtvSlot> reusedResult = heap.allocate();

    if (!reusedResult)
    {
        return false;
    }

    Result<void> staleResult = heap.release(original);
    const bool detected = has_error_code(staleResult.try_error(), 70);
    const bool released = static_cast<bool>(heap.release(*reusedResult.try_value()));
    return finish_heap(heap) && detected && released;
}

bool trigger_d3d12_rtv_heap_owner_violation_for_probe(D3d12RtvHeapOwnerViolationProbeMode a_mode,
                                                      const AssertContext &a_assertContext) noexcept
{
    reset_probe_state(ProbeCreationFault::None);
    Result<ProbeDevice> deviceResult = create_probe_device(false, a_assertContext);

    if (!deviceResult)
    {
        return false;
    }

    ProbeDevice device = std::move(*deviceResult.try_value());
    D3d12RtvHeapNativeFunctions functions = make_probe_functions();
    Result<D3d12RtvHeap> firstResult = create_d3d12_rtv_heap(device.device.Get(), a_assertContext, functions);

    if (!firstResult)
    {
        return false;
    }

    D3d12RtvHeap first = std::move(*firstResult.try_value());

    if (a_mode == D3d12RtvHeapOwnerViolationProbeMode::DestructorBeforeShutdown)
    {
        return true;
    }

    Result<D3d12RtvHeap> secondResult = create_d3d12_rtv_heap(device.device.Get(), a_assertContext, functions);

    if (!secondResult)
    {
        static_cast<void>(first.shutdown());
        return false;
    }

    D3d12RtvHeap second = std::move(*secondResult.try_value());
    first = std::move(second);
    return false;
}
} // namespace cue
