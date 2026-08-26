#include <Cue/RHI/D3D12/TestSupport/D3d12FrameCommandProbe.h>

#include "D3d12AdapterSelection.h"
#include "D3d12DeviceCreation.h"
#include "D3d12Diagnostics.h"
#include "D3d12FrameCommandState.h"
#include "D3d12ProbeUtilities.h"
#include "D3d12QueueState.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

using cue::d3d12_test_private::count_info_queue_errors;

namespace
{
constexpr std::uint32_t k_smokeIterationCount = 300;
constexpr HRESULT k_probeFailure = E_FAIL;

enum class ProbeFaultMode
{
    None,
    AllocatorReset,
    CommandListReset,
    CommandListClose,
    DiscardCommandListReset,
    DiscardCommandListClose,
};

enum class ProbeWaitFaultMode
{
    None,
    Registration,
    Timeout,
    Failed,
    Unexpected,
    EventIncomplete,
    EventClose,
    EventCreate,
    Unavailable,
    DeviceRemoved,
    FollowupNoSignal,
};

struct ProbeNativeState final
{
    ProbeFaultMode faultMode = ProbeFaultMode::None;
    ProbeWaitFaultMode waitFaultMode = ProbeWaitFaultMode::None;
    std::array<ID3D12CommandAllocator *, cue::k_d3d12FrameContextCount> allocators = {};
    std::array<ID3D12Resource *, cue::k_d3d12FrameContextCount> backBuffers = {};
    std::array<SIZE_T, cue::k_d3d12FrameContextCount> clearHandles = {};
    std::array<std::array<float, 4>, cue::k_d3d12FrameContextCount> clearColors = {};
    std::array<std::uint32_t, cue::k_d3d12FrameContextCount> allocatorResetCounts = {};
    std::array<bool, cue::k_d3d12FrameContextCount> completionCheckedSinceSubmit = {true, true};
    std::array<std::uint64_t, cue::k_d3d12FrameContextCount> submittedFenceValues = {};
    std::array<bool, cue::k_d3d12FrameContextCount> allocatorNamesContainIndex = {};
    std::uint32_t allocatorCreationCount = 0;
    std::uint32_t commandListResetCount = 0;
    std::uint32_t commandListCloseCount = 0;
    std::uint32_t executeCount = 0;
    std::uint32_t barrierCount = 0;
    std::uint32_t markerCount = 0;
    std::uint32_t clearCount = 0;
    std::uint32_t completedCallCount = 0;
    std::uint32_t createEventCallCount = 0;
    std::uint32_t closeHandleCallCount = 0;
    std::uint32_t setEventCallCount = 0;
    std::uint32_t nextFrameIndex = 0;
    std::uint64_t targetFenceValue = 0;
    bool fenceCheckedBeforeAllocatorReuse = true;
    bool waitFaultActive = false;
    bool signalFaultActive = false;
    bool frameSignalPending = false;
    bool barriersAreValid = true;
    bool markerNamesAreValid = true;
    bool clearArgumentsAreValid = true;
    bool clearOrderIsValid = true;
    HANDLE initialEvent = nullptr;
    HANDLE replacementEvent = nullptr;
    HANDLE lastRegisteredEvent = nullptr;
};

thread_local ProbeNativeState g_probeState;

/// @brief Probe 間で状態が混ざらないよう Fault Injection 用 Global 状態を初期化する
void reset_probe_state(ProbeFaultMode a_mode) noexcept
{
    g_probeState = {};
    g_probeState.faultMode = a_mode;
    g_probeState.completionCheckedSinceSubmit = {true, true};
}

/// @brief Native Allocator を生成し、Frame Slot ごとの再利用順序を検証できるよう参照を記録する
HRESULT create_allocator_for_probe(ID3D12Device *a_device, ID3D12CommandAllocator **a_allocator) noexcept
{
    const HRESULT result = a_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a_allocator));

    if (SUCCEEDED(result) && g_probeState.allocatorCreationCount < cue::k_d3d12FrameContextCount)
    {
        g_probeState.allocators[g_probeState.allocatorCreationCount] = *a_allocator;
        ++g_probeState.allocatorCreationCount;
    }

    return result;
}

/// @brief Native Command List の生成を転送し、Frame Command 初期化経路を実デバイスで検証する
HRESULT create_list_for_probe(ID3D12Device *a_device, ID3D12CommandAllocator *a_allocator,
                              ID3D12GraphicsCommandList **a_commandList) noexcept
{
    return a_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a_allocator, nullptr,
                                       IID_PPV_ARGS(a_commandList));
}

/// @brief Allocator 名に Frame Index が含まれるか記録してから Native SetName へ転送する
HRESULT set_name_for_probe(ID3D12Object *a_object, LPCWSTR a_name) noexcept
{
    for (std::uint32_t index = 0; index < cue::k_d3d12FrameContextCount; ++index)
    {
        if (a_object == g_probeState.allocators[index])
        {
            const std::wstring_view name(a_name != nullptr ? a_name : L"");
            const std::wstring_view expected = index == 0 ? L"Frame 0" : L"Frame 1";
            g_probeState.allocatorNamesContainIndex[index] = name.find(expected) != std::wstring_view::npos;
        }
    }

    return a_object->SetName(a_name);
}

/// @brief Fence 完了確認後だけ Allocator が再利用されるか記録し、指定時は Reset 失敗を注入する
HRESULT reset_allocator_for_probe(ID3D12CommandAllocator *a_allocator) noexcept
{
    for (std::uint32_t index = 0; index < cue::k_d3d12FrameContextCount; ++index)
    {
        if (a_allocator == g_probeState.allocators[index])
        {
            if (g_probeState.allocatorResetCounts[index] > 0 && !g_probeState.completionCheckedSinceSubmit[index])
            {
                g_probeState.fenceCheckedBeforeAllocatorReuse = false;
            }

            ++g_probeState.allocatorResetCounts[index];

            if (g_probeState.faultMode == ProbeFaultMode::AllocatorReset)
            {
                return k_probeFailure;
            }
        }
    }

    return a_allocator->Reset();
}

/// @brief Command List の Reset 回数を記録し、通常経路または破棄経路へ指定した失敗を注入する
HRESULT reset_list_for_probe(ID3D12GraphicsCommandList *a_commandList, ID3D12CommandAllocator *a_allocator) noexcept
{
    ++g_probeState.commandListResetCount;

    if (g_probeState.faultMode == ProbeFaultMode::CommandListReset ||
        (g_probeState.faultMode == ProbeFaultMode::DiscardCommandListReset && g_probeState.commandListResetCount == 2))
    {
        return k_probeFailure;
    }

    return a_commandList->Reset(a_allocator, nullptr);
}

/// @brief Command List の Close 回数を記録し、通常経路または破棄経路へ指定した失敗を注入する
HRESULT close_list_for_probe(ID3D12GraphicsCommandList *a_commandList) noexcept
{
    ++g_probeState.commandListCloseCount;

    if (g_probeState.faultMode == ProbeFaultMode::CommandListClose && g_probeState.commandListCloseCount == 2)
    {
        return k_probeFailure;
    }

    if (g_probeState.faultMode == ProbeFaultMode::DiscardCommandListClose && g_probeState.commandListCloseCount == 3)
    {
        return k_probeFailure;
    }

    return a_commandList->Close();
}

/// @brief Probe 対象の Resource Barrier 記録を Native Command List へ転送する
void resource_barrier_for_probe(ID3D12GraphicsCommandList *a_commandList, UINT a_barrierCount,
                                const D3D12_RESOURCE_BARRIER *a_barriers) noexcept
{
    const bool isRenderTargetTransition = (g_probeState.barrierCount % 2) == 0;
    const D3D12_RESOURCE_STATES expectedBefore =
        isRenderTargetTransition ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_RENDER_TARGET;
    const D3D12_RESOURCE_STATES expectedAfter =
        isRenderTargetTransition ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PRESENT;
    const D3D12_RESOURCE_BARRIER *barrier = a_barrierCount == 1 && a_barriers != nullptr ? &a_barriers[0] : nullptr;
    g_probeState.barriersAreValid =
        g_probeState.barriersAreValid && barrier != nullptr &&
        barrier->Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION &&
        barrier->Flags == D3D12_RESOURCE_BARRIER_FLAG_NONE &&
        barrier->Transition.pResource == g_probeState.backBuffers[g_probeState.nextFrameIndex] &&
        barrier->Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
        barrier->Transition.StateBefore == expectedBefore && barrier->Transition.StateAfter == expectedAfter;
    g_probeState.barrierCount += a_barrierCount;
    a_commandList->ResourceBarrier(a_barrierCount, a_barriers);
}

/// @brief Clear Marker の名前と Barrier 後の記録順を検証してから Native Marker 呼び出しへ転送する
void set_marker_for_probe(ID3D12GraphicsCommandList *a_commandList, PCSTR a_name) noexcept
{
    g_probeState.markerNamesAreValid =
        g_probeState.markerNamesAreValid && a_name != nullptr && std::strcmp(a_name, "ClearBackBuffer") == 0;
    g_probeState.clearOrderIsValid =
        g_probeState.clearOrderIsValid && g_probeState.barrierCount == (g_probeState.markerCount * 2) + 1;
    ++g_probeState.markerCount;
    cue::default_d3d12_frame_command_native_functions().setMarker(a_commandList, a_name);
}

/// @brief Probe 対象の RTV Clear を Native Command List へ転送して引数を観測する
void clear_render_target_view_for_probe(ID3D12GraphicsCommandList *a_commandList,
                                        D3D12_CPU_DESCRIPTOR_HANDLE a_handle, const FLOAT a_color[4],
                                        UINT a_rectangleCount, const D3D12_RECT *a_rectangles) noexcept
{
    const std::uint32_t frameIndex = g_probeState.nextFrameIndex;
    g_probeState.clearArgumentsAreValid =
        g_probeState.clearArgumentsAreValid && frameIndex < cue::k_d3d12FrameContextCount &&
        a_handle.ptr == g_probeState.clearHandles[frameIndex] && a_color != nullptr && a_rectangleCount == 0 &&
        a_rectangles == nullptr;
    g_probeState.clearOrderIsValid =
        g_probeState.clearOrderIsValid && g_probeState.markerCount == g_probeState.clearCount + 1 &&
        g_probeState.barrierCount == (g_probeState.clearCount * 2) + 1;

    if (a_color != nullptr && g_probeState.clearCount < cue::k_d3d12FrameContextCount)
    {
        std::copy_n(a_color, 4, g_probeState.clearColors[g_probeState.clearCount].begin());
    }

    ++g_probeState.clearCount;
    cue::default_d3d12_frame_command_native_functions().clearRenderTargetView(
        a_commandList, a_handle, a_color, a_rectangleCount, a_rectangles);
}

/// @brief Command List 投入を記録して対象 Frame Slot を未完了状態にし、Native Queue へ転送する
void execute_lists_for_probe(ID3D12CommandQueue *a_queue, UINT a_count,
                             ID3D12CommandList *const *a_commandLists) noexcept
{
    ++g_probeState.executeCount;
    g_probeState.completionCheckedSinceSubmit[g_probeState.nextFrameIndex] = false;
    g_probeState.frameSignalPending = true;
    a_queue->ExecuteCommandLists(a_count, a_commandLists);
}

/// @brief Fault Probe が指定した Fence 完了値を Native Queue 経路へ返す
std::uint64_t completed_value_for_probe(ID3D12Fence *a_fence) noexcept
{
    ++g_probeState.completedCallCount;
    std::uint64_t completedValue = a_fence->GetCompletedValue();

    if (g_probeState.waitFaultActive)
    {
        if (g_probeState.waitFaultMode == ProbeWaitFaultMode::Unavailable ||
            g_probeState.waitFaultMode == ProbeWaitFaultMode::DeviceRemoved ||
            g_probeState.waitFaultMode == ProbeWaitFaultMode::FollowupNoSignal)
        {
            completedValue = 0;
        }
        else if (g_probeState.completedCallCount == 1 ||
                 (g_probeState.waitFaultMode == ProbeWaitFaultMode::EventIncomplete &&
                  g_probeState.completedCallCount == 2))
        {
            completedValue = 0;
        }
        else if (g_probeState.targetFenceValue != 0)
        {
            completedValue = g_probeState.targetFenceValue;
        }
    }

    g_probeState.completionCheckedSinceSubmit[g_probeState.nextFrameIndex] =
        completedValue >= g_probeState.submittedFenceValues[g_probeState.nextFrameIndex];
    return completedValue;
}

/// @brief Fence Event 登録を記録し、指定時は登録失敗を注入して待機 Error 経路を再現する
HRESULT set_event_for_probe(ID3D12Fence *a_fence, std::uint64_t a_value, HANDLE a_event) noexcept
{
    ++g_probeState.setEventCallCount;
    g_probeState.lastRegisteredEvent = a_event;

    if (g_probeState.waitFaultActive && g_probeState.waitFaultMode == ProbeWaitFaultMode::Registration)
    {
        return E_FAIL;
    }

    return a_fence->SetEventOnCompletion(a_value, a_event);
}

/// @brief 指定した待機結果を注入し、それ以外は Win32 Event 待機を実行して Error 分岐を検証可能にする
DWORD WINAPI wait_for_single_object_for_probe(HANDLE a_event, DWORD a_timeout)
{
    if (g_probeState.waitFaultActive)
    {
        switch (g_probeState.waitFaultMode)
        {
        case ProbeWaitFaultMode::Timeout:
            return WAIT_TIMEOUT;
        case ProbeWaitFaultMode::Failed:
            return WAIT_FAILED;
        case ProbeWaitFaultMode::Unexpected:
            return WAIT_ABANDONED;
        case ProbeWaitFaultMode::EventIncomplete:
            return WAIT_OBJECT_0;
        case ProbeWaitFaultMode::EventClose:
        case ProbeWaitFaultMode::EventCreate:
        case ProbeWaitFaultMode::Unavailable:
        case ProbeWaitFaultMode::DeviceRemoved:
        case ProbeWaitFaultMode::FollowupNoSignal:
            return WAIT_TIMEOUT;
        default:
            break;
        }
    }

    return WaitForSingleObject(a_event, a_timeout);
}

/// @brief WAIT_FAILED 経路の Native Error 伝播を検証するため固定 Win32 Error Code を返す
DWORD WINAPI get_last_error_for_probe()
{
    return 1234;
}

/// @brief Event 再生成回数と Handle を記録し、指定時は交換用 Event の生成失敗を注入する
HANDLE WINAPI create_event_for_probe(LPSECURITY_ATTRIBUTES a_attributes, BOOL a_manualReset, BOOL a_initialState,
                                     LPCWSTR a_name)
{
    ++g_probeState.createEventCallCount;

    if (g_probeState.waitFaultActive && g_probeState.waitFaultMode == ProbeWaitFaultMode::EventCreate &&
        g_probeState.createEventCallCount > 1)
    {
        return nullptr;
    }

    HANDLE eventHandle = CreateEventW(a_attributes, a_manualReset, a_initialState, a_name);

    if (g_probeState.createEventCallCount == 1)
    {
        g_probeState.initialEvent = eventHandle;
    }
    else
    {
        g_probeState.replacementEvent = eventHandle;
    }

    return eventHandle;
}

/// @brief Event Handle の Close 回数を記録し、指定時は Close 失敗を注入する
BOOL WINAPI close_handle_for_probe(HANDLE a_handle)
{
    ++g_probeState.closeHandleCallCount;

    if (g_probeState.waitFaultActive && g_probeState.waitFaultMode == ProbeWaitFaultMode::EventClose)
    {
        return FALSE;
    }

    return CloseHandle(a_handle);
}

/// @brief 指定時は Device Removal を注入し、それ以外は実デバイスの Removal 理由を返す
HRESULT get_device_removed_reason_for_probe(ID3D12Device *a_device) noexcept
{
    if (g_probeState.waitFaultActive && g_probeState.waitFaultMode == ProbeWaitFaultMode::DeviceRemoved)
    {
        return DXGI_ERROR_DEVICE_REMOVED;
    }

    return a_device->GetDeviceRemovedReason();
}

/// @brief Signal 対象値と Frame Slot の完了追跡値を記録し、指定時は Native Signal を抑止する
HRESULT signal_for_probe(ID3D12CommandQueue *a_queue, ID3D12Fence *a_fence, std::uint64_t a_value) noexcept
{
    g_probeState.targetFenceValue = a_value;
    HRESULT result = S_OK;

    if (g_probeState.waitFaultActive && g_probeState.waitFaultMode == ProbeWaitFaultMode::FollowupNoSignal)
    {
        result = S_OK;
    }
    else
    {
        result = a_queue->Signal(a_fence, a_value);
    }

    if (g_probeState.frameSignalPending)
    {
        g_probeState.submittedFenceValues[g_probeState.nextFrameIndex] = a_value;
        g_probeState.frameSignalPending = false;
    }

    if (g_probeState.signalFaultActive)
    {
        return k_probeFailure;
    }

    return result;
}

/// @brief D3D12 Frame Command Probe で使用する Frame Functions を生成し、呼び出し元へ返す
[[nodiscard]] cue::D3d12FrameCommandNativeFunctions make_frame_functions() noexcept
{
    return {
        create_allocator_for_probe, create_list_for_probe, set_name_for_probe,
        reset_allocator_for_probe,  reset_list_for_probe,  close_list_for_probe,
        resource_barrier_for_probe, set_marker_for_probe,   clear_render_target_view_for_probe,
    };
}

/// @brief D3D12 Frame Command Probe で使用する Queue Functions を生成し、呼び出し元へ返す
[[nodiscard]] cue::D3d12QueueNativeFunctions make_queue_functions() noexcept
{
    cue::D3d12QueueNativeFunctions functions = cue::default_d3d12_queue_native_functions();
    functions.executeCommandLists = execute_lists_for_probe;
    functions.getCompletedValue = completed_value_for_probe;
    functions.signal = signal_for_probe;
    functions.setEventOnCompletion = set_event_for_probe;
    functions.waitForSingleObject = wait_for_single_object_for_probe;
    functions.createEvent = create_event_for_probe;
    functions.closeHandle = close_handle_for_probe;
    functions.getLastError = get_last_error_for_probe;
    functions.getDeviceRemovedReason = get_device_removed_reason_for_probe;
    return functions;
}

struct ProbeObjects final
{
    /// @brief D3D12 Frame Command Probe に必要な Native Object と State の所有権を束ねる
    ProbeObjects(Microsoft::WRL::ComPtr<ID3D12Device> a_device, cue::D3d12QueueState &&a_queueState,
                 cue::D3d12DiagnosticsStatus a_diagnostics) noexcept
        : device(std::move(a_device)), queueState(std::move(a_queueState)), diagnostics(a_diagnostics)
    {
    }

    /// @brief ProbeObjects の一意所有を保つため Copy 構築を禁止する
    ProbeObjects(const ProbeObjects &) = delete;
    /// @brief ProbeObjects の一意所有を保つため Copy 代入を禁止する
    ProbeObjects &operator=(const ProbeObjects &) = delete;
    /// @brief ProbeObjects の所有状態を移動させないため Move 構築を禁止する
    ProbeObjects(ProbeObjects &&) noexcept = delete;
    /// @brief ProbeObjects の所有状態を移動させないため Move 代入を禁止する
    ProbeObjects &operator=(ProbeObjects &&) noexcept = delete;
    /// @brief ProbeObjects が保持する Resource を所有権規則に従って破棄する
    ~ProbeObjects() noexcept = default;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    cue::D3d12QueueState queueState;
    std::unique_ptr<cue::D3d12FrameCommandState> frameState;
    cue::D3d12DiagnosticsStatus diagnostics;
};

/// @brief D3D12 Frame Command Probe の Probe Back Buffers を所有権と Lifecycle 規則を守って関連付ける
[[nodiscard]] cue::Result<void> bind_probe_back_buffers(ProbeObjects &a_objects,
                                                        const cue::AssertContext &a_assertContext) noexcept
{
    D3D12_HEAP_PROPERTIES heapProperties = {};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC resourceDescriptor = {};
    resourceDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDescriptor.Width = 4;
    resourceDescriptor.Height = 4;
    resourceDescriptor.DepthOrArraySize = 1;
    resourceDescriptor.MipLevels = 1;
    resourceDescriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resourceDescriptor.SampleDesc.Count = 1;
    resourceDescriptor.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDescriptor.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    cue::D3d12FrameBackBuffers backBuffers = {};

    for (std::uint32_t index = 0; index < cue::k_d3d12FrameContextCount; ++index)
    {
        const HRESULT result = a_objects.device->CreateCommittedResource(
            &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDescriptor, D3D12_RESOURCE_STATE_PRESENT, nullptr,
            IID_PPV_ARGS(&backBuffers[index]));

        if (FAILED(result))
        {
            return cue::Result<void>::failure(cue::Error::create(
                a_assertContext.fatal_handler(),
                cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.RHI.D3D12", 96),
                "D3D12 Back Buffer Transition probe Resource creation failed"));
        }

        g_probeState.backBuffers[index] = backBuffers[index].Get();
    }

    return a_objects.frameState->bind_back_buffers(std::move(backBuffers));
}

/// @brief D3D12 Frame Command Probe で使用する Probe Objects を生成し、呼び出し元へ返す
[[nodiscard]] cue::Result<std::unique_ptr<ProbeObjects>> create_probe_objects(
    ProbeFaultMode a_mode, bool a_enableDiagnostics, const cue::AssertContext &a_assertContext) noexcept
{
    reset_probe_state(a_mode);
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
        return cue::Result<std::unique_ptr<ProbeObjects>>::failure(std::move(*diagnosticsResult.try_error()));
    }

    cue::D3d12DiagnosticsStatus diagnostics = *diagnosticsResult.try_value();
    cue::Result<cue::D3d12AdapterSelection> selectionResult =
        cue::select_d3d12_adapter(cue::D3d12AdapterPolicy::Warp, diagnostics, a_assertContext);

    if (!selectionResult)
    {
        return cue::Result<std::unique_ptr<ProbeObjects>>::failure(std::move(*selectionResult.try_error()));
    }

    cue::D3d12AdapterSelection selection = std::move(*selectionResult.try_value());
    cue::Result<Microsoft::WRL::ComPtr<ID3D12Device>> deviceResult =
        cue::create_d3d12_device(selection.adapter.Get(), selection.featureLevel, a_assertContext);

    if (!deviceResult)
    {
        return cue::Result<std::unique_ptr<ProbeObjects>>::failure(std::move(*deviceResult.try_error()));
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device = std::move(*deviceResult.try_value());
    cue::Result<void> infoQueueResult = cue::configure_d3d12_info_queue(device.Get(), diagnostics, a_assertContext);

    if (!infoQueueResult)
    {
        return cue::Result<std::unique_ptr<ProbeObjects>>::failure(std::move(*infoQueueResult.try_error()));
    }

    cue::D3d12QueueNativeFunctions queueFunctions = make_queue_functions();
    cue::Result<cue::D3d12QueueState> queueResult = cue::create_d3d12_queue_state(
        device.Get(), descriptor.gpuWaitTimeoutMilliseconds, a_assertContext, queueFunctions);

    if (!queueResult)
    {
        return cue::Result<std::unique_ptr<ProbeObjects>>::failure(std::move(*queueResult.try_error()));
    }

    cue::D3d12QueueState queueState = std::move(*queueResult.try_value());
    std::unique_ptr<ProbeObjects> objects =
        std::make_unique<ProbeObjects>(std::move(device), std::move(queueState), diagnostics);
    cue::D3d12FrameCommandNativeFunctions frameFunctions = make_frame_functions();
    cue::Result<cue::D3d12FrameCommandState> frameResult = cue::create_d3d12_frame_command_state(
        objects->device.Get(), objects->queueState, a_assertContext, frameFunctions);

    if (!frameResult)
    {
        cue::Result<void> queueShutdownResult = objects->queueState.shutdown();
        static_cast<void>(queueShutdownResult);
        return cue::Result<std::unique_ptr<ProbeObjects>>::failure(std::move(*frameResult.try_error()));
    }

    objects->frameState = std::make_unique<cue::D3d12FrameCommandState>(std::move(*frameResult.try_value()));
    return cue::Result<std::unique_ptr<ProbeObjects>>::success(std::move(objects));
}

/// @brief D3D12 Frame Command Probe の Probe Objects を依存関係と完了条件を守って安全に解放または停止する
[[nodiscard]] bool shutdown_probe_objects(std::unique_ptr<ProbeObjects> &a_objects) noexcept
{
    cue::Result<void> frameShutdownResult = a_objects->frameState->begin_shutdown();

    if (a_objects->frameState->status() != cue::D3d12FrameCommandStatus::CleanupPending)
    {
        return false;
    }

    a_objects->frameState->release_back_buffers();
    cue::Result<void> allocatorResult = a_objects->frameState->release_allocators_after_presentation_cleanup();

    cue::Result<void> queueShutdownResult = a_objects->queueState.shutdown();
    return frameShutdownResult && allocatorResult && queueShutdownResult;
}

/// @brief D3D12 Frame Command Probe の Probe Faults を診断と Lifecycle の規則に従って更新する
void disable_probe_faults() noexcept
{
    g_probeState.waitFaultMode = ProbeWaitFaultMode::None;
    g_probeState.waitFaultActive = false;
    g_probeState.signalFaultActive = false;
    g_probeState.completedCallCount = 0;
}

/// @brief D3D12 Frame Command Probe の Probe Objects を依存関係と完了条件を守って安全に解放または停止する
[[nodiscard]] bool finish_probe_objects(std::unique_ptr<ProbeObjects> &&a_objects, bool a_valid) noexcept
{
    if (!a_objects)
    {
        return a_valid;
    }

    disable_probe_faults();

    if (a_objects->frameState->status() == cue::D3d12FrameCommandStatus::Ready)
    {
        cue::Result<void> frameShutdownResult = a_objects->frameState->shutdown();
        a_valid = a_valid && static_cast<bool>(frameShutdownResult);
    }
    else if (a_objects->frameState->status() == cue::D3d12FrameCommandStatus::DeviceRemoved)
    {
        cue::Result<void> frameReleaseResult = a_objects->frameState->release_after_device_removed();
        a_valid = a_valid && static_cast<bool>(frameReleaseResult);
    }

    if (a_objects->frameState->status() == cue::D3d12FrameCommandStatus::Unavailable)
    {
        static_cast<void>(a_objects.release());
        return a_valid;
    }

    if (a_objects->queueState.status() == cue::D3d12QueueStateStatus::Ready)
    {
        cue::Result<void> queueShutdownResult = a_objects->queueState.shutdown();
        a_valid = a_valid && static_cast<bool>(queueShutdownResult);
    }
    else if (a_objects->queueState.status() == cue::D3d12QueueStateStatus::DeviceRemoved)
    {
        cue::Result<void> queueReleaseResult = a_objects->queueState.release_after_device_removed();
        a_valid = a_valid && static_cast<bool>(queueReleaseResult);
    }

    if (a_objects->queueState.status() == cue::D3d12QueueStateStatus::Unavailable)
    {
        static_cast<void>(a_objects.release());
    }

    return a_valid;
}

/// @brief Error の抽象 Code と Native Error が期待値に一致するかを判定する
[[nodiscard]] bool matches_error(const cue::Error *a_error, std::int64_t a_code) noexcept
{
    return a_error != nullptr && a_error->code().domain() == "Cue.RHI.D3D12" && a_error->code().value() == a_code;
}

/// @brief D3D12 Frame Command Probe の Native Error 条件を判定して返す
[[nodiscard]] bool matches_native_error(const cue::Error *a_error, std::int64_t a_code, HRESULT a_nativeCode) noexcept
{
    const cue::NativeError *nativeError = a_error != nullptr ? a_error->try_native_error() : nullptr;
    return matches_error(a_error, a_code) && nativeError != nullptr && nativeError->domain() == "D3D12" &&
           nativeError->value() == static_cast<std::int64_t>(a_nativeCode);
}

/// @brief Error に指定された診断 Context が含まれるかを判定する
[[nodiscard]] bool has_error_context(const cue::Error *a_error, std::string_view a_context) noexcept
{
    if (a_error == nullptr)
    {
        return false;
    }

    for (const cue::ErrorContext &context : a_error->contexts())
    {
        if (context.message() == a_context)
        {
            return true;
        }
    }

    return false;
}

/// @brief D3D12 Frame Command Probe の Present Signal For Probe を GPU 実行順と Resource State を守って投入する
[[nodiscard]] cue::Result<std::uint64_t> execute_present_signal_for_probe(
    cue::D3d12FrameCommandState &a_frameState) noexcept
{
    cue::Result<void> executeResult = a_frameState.execute_frame();

    if (!executeResult)
    {
        return cue::Result<std::uint64_t>::failure(std::move(*executeResult.try_error()));
    }

    cue::Result<void> presentResult = a_frameState.mark_present_attempted();

    if (!presentResult)
    {
        return cue::Result<std::uint64_t>::failure(std::move(*presentResult.try_error()));
    }

    return a_frameState.signal_frame();
}

/// @brief D3D12 Frame Command Probe の Empty Frame For Probe を GPU 実行順と Resource State を守って投入する
[[nodiscard]] cue::Result<std::uint64_t> submit_empty_frame_for_probe(ProbeObjects &a_objects,
                                                                      std::uint32_t a_frameIndex) noexcept
{
    g_probeState.nextFrameIndex = a_frameIndex;
    cue::Result<void> beginResult = a_objects.frameState->begin_frame(a_frameIndex);

    if (!beginResult)
    {
        return cue::Result<std::uint64_t>::failure(std::move(*beginResult.try_error()));
    }

    cue::Result<void> closeResult = a_objects.frameState->close_frame();

    if (!closeResult)
    {
        return cue::Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
    }

    return execute_present_signal_for_probe(*a_objects.frameState);
}
} // namespace

namespace cue
{
Result<D3d12FrameCommandProbeReport> probe_d3d12_frame_commands(const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, true, a_assertContext);

    if (!objectsResult)
    {
        return Result<D3d12FrameCommandProbeReport>::failure(std::move(*objectsResult.try_error()));
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    Result<void> bindingResult = bind_probe_back_buffers(*objects, a_assertContext);

    if (!bindingResult)
    {
        return Result<D3d12FrameCommandProbeReport>::failure(std::move(*bindingResult.try_error()));
    }

    for (std::uint32_t iteration = 0; iteration < k_smokeIterationCount; ++iteration)
    {
        const std::uint32_t frameIndex = iteration % k_d3d12FrameContextCount;
        g_probeState.nextFrameIndex = frameIndex;
        Result<void> beginResult = objects->frameState->begin_frame(frameIndex);

        if (!beginResult)
        {
            return Result<D3d12FrameCommandProbeReport>::failure(std::move(*beginResult.try_error()));
        }

        Result<void> renderTargetResult =
            objects->frameState->transition_back_buffer(frameIndex, D3d12BackBufferState::RenderTarget);
        Result<void> sameStateResult =
            renderTargetResult
                ? objects->frameState->transition_back_buffer(frameIndex, D3d12BackBufferState::RenderTarget)
                : Result<void>::failure(std::move(*renderTargetResult.try_error()));
        Result<void> presentResult =
            sameStateResult ? objects->frameState->transition_back_buffer(frameIndex, D3d12BackBufferState::Present)
                            : Result<void>::failure(std::move(*sameStateResult.try_error()));

        if (!presentResult)
        {
            return Result<D3d12FrameCommandProbeReport>::failure(std::move(*presentResult.try_error()));
        }

        Result<void> closeResult = objects->frameState->close_frame();

        if (!closeResult)
        {
            return Result<D3d12FrameCommandProbeReport>::failure(std::move(*closeResult.try_error()));
        }

        Result<std::uint64_t> submitResult = execute_present_signal_for_probe(*objects->frameState);

        if (!submitResult)
        {
            return Result<D3d12FrameCommandProbeReport>::failure(std::move(*submitResult.try_error()));
        }
    }

    const std::uint64_t lastSubmittedFence = objects->frameState->last_submitted_fence();
    const bool backBuffersReturnedToPresent = objects->frameState->are_back_buffers_present();

    if (!shutdown_probe_objects(objects))
    {
        return Result<D3d12FrameCommandProbeReport>::failure(Error::create(
            a_assertContext.fatal_handler(), ErrorCode::create(a_assertContext.fatal_handler(), "Cue.RHI.D3D12", 65),
            "D3D12 Frame Command smoke shutdown failed"));
    }

    const std::uint64_t infoQueueErrorCount = count_info_queue_errors(objects->device.Get());
    Result<void> messageResult = log_d3d12_messages_at_quiescent_point(objects->device.Get(), objects->diagnostics,
                                                                       "D3D12 Frame Command smoke", a_assertContext);

    if (!messageResult)
    {
        return Result<D3d12FrameCommandProbeReport>::failure(std::move(*messageResult.try_error()));
    }

    D3d12FrameCommandProbeReport report = {};
    report.iterationCount = k_smokeIterationCount;
    report.frameZeroResetCount = g_probeState.allocatorResetCounts[0];
    report.frameOneResetCount = g_probeState.allocatorResetCounts[1];
    report.executeCount = g_probeState.executeCount;
    report.barrierCount = g_probeState.barrierCount;
    report.lastSubmittedFence = lastSubmittedFence;
    report.infoQueueErrorCount = infoQueueErrorCount;
    report.frameNamesContainIndices =
        g_probeState.allocatorNamesContainIndex[0] && g_probeState.allocatorNamesContainIndex[1];
    report.fenceCheckedBeforeAllocatorReuse = g_probeState.fenceCheckedBeforeAllocatorReuse;
    report.barriersAreValid = g_probeState.barriersAreValid;
    report.backBuffersReturnedToPresent = backBuffersReturnedToPresent;
    report.diagnosticsAvailable = objects->diagnostics.isInfoQueueEnabled;
    return Result<D3d12FrameCommandProbeReport>::success(std::move(report));
}

bool verify_d3d12_back_buffer_clear_for_probe(const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, true, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    Result<void> backBufferResult = bind_probe_back_buffers(*objects, a_assertContext);

    if (!backBufferResult)
    {
        return shutdown_probe_objects(objects) && false;
    }

    Result<D3d12RtvHeap> heapResult = create_d3d12_rtv_heap(objects->device.Get(), a_assertContext);

    if (!heapResult)
    {
        return shutdown_probe_objects(objects) && false;
    }

    D3d12RtvHeap heap = std::move(*heapResult.try_value());
    bool valid = true;

    for (std::uint32_t index = 0; index < k_d3d12FrameContextCount && valid; ++index)
    {
        Result<D3d12RtvSlot> slotResult = heap.allocate();

        if (!slotResult)
        {
            valid = false;
            break;
        }

        const D3d12RtvSlot slot = *slotResult.try_value();
        Result<D3D12_CPU_DESCRIPTOR_HANDLE> handleResult = heap.cpu_handle(slot);
        Result<ID3D12Resource *> bufferResult = objects->frameState->back_buffer(index);

        if (!handleResult || !bufferResult)
        {
            static_cast<void>(heap.release(slot));
            valid = false;
            break;
        }

        D3D12_RENDER_TARGET_VIEW_DESC descriptor = {};
        descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        descriptor.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        descriptor.Texture2D.MipSlice = 0;
        descriptor.Texture2D.PlaneSlice = 0;
        objects->device->CreateRenderTargetView(*bufferResult.try_value(), &descriptor, *handleResult.try_value());
        Result<void> bindingResult = objects->frameState->bind_rtv_slot(index, slot);

        if (!bindingResult)
        {
            static_cast<void>(heap.release(slot));
            valid = false;
            break;
        }

        g_probeState.clearHandles[index] = handleResult.try_value()->ptr;
    }

    constexpr std::array<std::array<float, 4>, k_d3d12FrameContextCount> colors = {
        std::array<float, 4>{0.125F, 0.25F, 0.5F, 1.0F},
        std::array<float, 4>{0.75F, 0.375F, 0.0625F, 1.0F},
    };

    for (std::uint32_t frameIndex = 0; frameIndex < k_d3d12FrameContextCount && valid; ++frameIndex)
    {
        g_probeState.nextFrameIndex = frameIndex;
        Result<void> beginResult = objects->frameState->begin_frame(frameIndex);
        Result<void> renderTargetResult =
            beginResult ? objects->frameState->transition_back_buffer(frameIndex, D3d12BackBufferState::RenderTarget)
                        : Result<void>::failure(std::move(*beginResult.try_error()));
        Result<void> clearResult = renderTargetResult
                                       ? objects->frameState->clear_back_buffer(frameIndex, heap, colors[frameIndex])
                                       : Result<void>::failure(std::move(*renderTargetResult.try_error()));
        Result<void> presentResult =
            clearResult ? objects->frameState->transition_back_buffer(frameIndex, D3d12BackBufferState::Present)
                        : Result<void>::failure(std::move(*clearResult.try_error()));
        Result<void> closeResult = presentResult ? objects->frameState->close_frame()
                                                 : Result<void>::failure(std::move(*presentResult.try_error()));
        Result<std::uint64_t> submitResult =
            closeResult ? execute_present_signal_for_probe(*objects->frameState)
                        : Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
        valid = static_cast<bool>(submitResult);
    }

    valid = valid && g_probeState.markerCount == k_d3d12FrameContextCount &&
            g_probeState.clearCount == k_d3d12FrameContextCount && g_probeState.markerNamesAreValid &&
            g_probeState.clearArgumentsAreValid && g_probeState.clearOrderIsValid &&
            g_probeState.clearColors == colors && g_probeState.barrierCount == 4 && g_probeState.barriersAreValid &&
            objects->frameState->are_back_buffers_present();
    Result<void> releaseResult = objects->frameState->release_rtv_slots(heap);
    Result<void> heapShutdownResult = heap.shutdown();
    valid = valid && releaseResult && heapShutdownResult && shutdown_probe_objects(objects);
    const std::uint64_t infoQueueErrorCount = count_info_queue_errors(objects->device.Get());
    Result<void> messageResult = log_d3d12_messages_at_quiescent_point(
        objects->device.Get(), objects->diagnostics, "D3D12 Back Buffer Clear probe", a_assertContext);
    return valid && infoQueueErrorCount == 0 && messageResult;
}

bool verify_d3d12_back_buffer_clear_rejection_for_probe(D3d12BackBufferClearRejectionProbeMode a_mode,
                                                         const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    Result<D3d12RtvHeap> heapResult = create_d3d12_rtv_heap(objects->device.Get(), a_assertContext);

    if (!heapResult)
    {
        return shutdown_probe_objects(objects) && false;
    }

    D3d12RtvHeap heap = std::move(*heapResult.try_value());
    const bool needsBackBuffer = a_mode == D3d12BackBufferClearRejectionProbeMode::PresentState ||
                                 a_mode == D3d12BackBufferClearRejectionProbeMode::MissingRtv ||
                                 a_mode == D3d12BackBufferClearRejectionProbeMode::InvalidRtvHandle;
    bool valid = !needsBackBuffer || bind_probe_back_buffers(*objects, a_assertContext);

    if (valid && a_mode == D3d12BackBufferClearRejectionProbeMode::PresentState)
    {
        Result<D3d12RtvSlot> slotResult = heap.allocate();
        Result<D3D12_CPU_DESCRIPTOR_HANDLE> handleResult =
            slotResult ? heap.cpu_handle(*slotResult.try_value())
                       : Result<D3D12_CPU_DESCRIPTOR_HANDLE>::failure(std::move(*slotResult.try_error()));
        Result<ID3D12Resource *> bufferResult = objects->frameState->back_buffer(0);

        if (handleResult && bufferResult)
        {
            D3D12_RENDER_TARGET_VIEW_DESC descriptor = {};
            descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            descriptor.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            descriptor.Texture2D.MipSlice = 0;
            descriptor.Texture2D.PlaneSlice = 0;
            objects->device->CreateRenderTargetView(*bufferResult.try_value(), &descriptor,
                                                    *handleResult.try_value());
            valid = static_cast<bool>(objects->frameState->bind_rtv_slot(0, *slotResult.try_value()));
        }
        else
        {
            if (slotResult)
            {
                static_cast<void>(heap.release(*slotResult.try_value()));
            }

            valid = false;
        }
    }
    else if (valid && a_mode == D3d12BackBufferClearRejectionProbeMode::InvalidRtvHandle)
    {
        const D3d12RtvSlot invalidSlot = {0, k_d3d12RtvDescriptorCapacity, 0};
        valid = static_cast<bool>(objects->frameState->bind_rtv_slot(0, invalidSlot));
    }

    constexpr std::array<float, 4> color = {0.25F, 0.5F, 0.75F, 1.0F};
    const std::uint32_t clearFrameIndex =
        a_mode == D3d12BackBufferClearRejectionProbeMode::NonCurrentFrame ? 1 : 0;
    Result<void> beginResult =
        a_mode == D3d12BackBufferClearRejectionProbeMode::OutsideRecording
            ? Result<void>::success()
            : objects->frameState->begin_frame(0);
    const bool needsRenderTarget = a_mode == D3d12BackBufferClearRejectionProbeMode::MissingRtv ||
                                   a_mode == D3d12BackBufferClearRejectionProbeMode::InvalidRtvHandle;
    Result<void> renderTargetResult =
        beginResult && needsRenderTarget
            ? objects->frameState->transition_back_buffer(0, D3d12BackBufferState::RenderTarget)
            : (beginResult ? Result<void>::success() : Result<void>::failure(std::move(*beginResult.try_error())));
    Result<void> clearResult =
        valid && renderTargetResult
            ? objects->frameState->clear_back_buffer(clearFrameIndex, heap, color)
            : Result<void>::failure(Error::create(
                  a_assertContext.fatal_handler(), ErrorCode::create(a_assertContext.fatal_handler(), "Probe", 1),
                  "D3D12 Back Buffer Clear rejection probe setup failed"));
    const std::int64_t expectedCode =
        a_mode == D3d12BackBufferClearRejectionProbeMode::InvalidRtvHandle ? 69 : 97;
    valid = !clearResult && matches_error(clearResult.try_error(), expectedCode) && g_probeState.markerCount == 0 &&
            g_probeState.clearCount == 0;

    if (needsRenderTarget && renderTargetResult)
    {
        valid = objects->frameState->transition_back_buffer(0, D3d12BackBufferState::Present) && valid;
    }

    if (a_mode != D3d12BackBufferClearRejectionProbeMode::OutsideRecording && beginResult)
    {
        Result<void> closeResult = objects->frameState->close_frame();
        Result<std::uint64_t> submitResult =
            closeResult ? execute_present_signal_for_probe(*objects->frameState)
                        : Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
        valid = submitResult && valid;
    }

    Result<void> releaseResult = objects->frameState->release_rtv_slots(heap);
    const bool expectedReleaseFailure = a_mode == D3d12BackBufferClearRejectionProbeMode::InvalidRtvHandle;
    valid = static_cast<bool>(releaseResult) == !expectedReleaseFailure && valid;
    Result<void> heapShutdownResult = heap.shutdown();
    return heapShutdownResult && shutdown_probe_objects(objects) && valid;
}

bool verify_d3d12_invalid_frame_index_for_probe(const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    Result<void> beginResult = objects->frameState->begin_frame(k_d3d12FrameContextCount);
    const bool valid = matches_error(beginResult.try_error(), 59) && g_probeState.allocatorResetCounts[0] == 0 &&
                       g_probeState.allocatorResetCounts[1] == 0 && g_probeState.executeCount == 0;
    return shutdown_probe_objects(objects) && valid && g_probeState.allocatorResetCounts[0] == 0 &&
           g_probeState.allocatorResetCounts[1] == 0 && g_probeState.executeCount == 0;
}

bool verify_d3d12_transition_invalid_index_for_probe(const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    Result<void> beginResult = objects->frameState->begin_frame(0);
    Result<void> transitionResult =
        beginResult ? objects->frameState->transition_back_buffer(k_d3d12FrameContextCount,
                                                                  D3d12BackBufferState::RenderTarget)
                    : Result<void>::failure(std::move(*beginResult.try_error()));
    const bool valid = matches_error(transitionResult.try_error(), 59) && g_probeState.barrierCount == 0;
    Result<void> closeResult = objects->frameState->close_frame();
    Result<std::uint64_t> submitResult =
        closeResult ? execute_present_signal_for_probe(*objects->frameState)
                    : Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
    return submitResult && shutdown_probe_objects(objects) && valid && g_probeState.barrierCount == 0;
}

bool verify_d3d12_transition_null_resource_for_probe(const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    Result<void> beginResult = objects->frameState->begin_frame(0);
    Result<void> transitionResult =
        beginResult ? objects->frameState->transition_back_buffer(0, D3d12BackBufferState::RenderTarget)
                    : Result<void>::failure(std::move(*beginResult.try_error()));
    const bool valid = matches_error(transitionResult.try_error(), 95) && g_probeState.barrierCount == 0;
    Result<void> closeResult = objects->frameState->close_frame();
    Result<std::uint64_t> submitResult =
        closeResult ? execute_present_signal_for_probe(*objects->frameState)
                    : Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
    return submitResult && shutdown_probe_objects(objects) && valid && g_probeState.barrierCount == 0;
}

bool verify_d3d12_transition_order_for_probe(D3d12BackBufferTransitionOrderProbeMode a_mode,
                                              const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());

    if (a_mode == D3d12BackBufferTransitionOrderProbeMode::OutsideRecording)
    {
        Result<void> transitionResult =
            objects->frameState->transition_back_buffer(0, D3d12BackBufferState::RenderTarget);
        const bool valid = matches_error(transitionResult.try_error(), 94) && g_probeState.barrierCount == 0;
        return shutdown_probe_objects(objects) && valid;
    }

    Result<void> beginResult = objects->frameState->begin_frame(0);
    Result<void> transitionResult =
        beginResult ? objects->frameState->transition_back_buffer(1, D3d12BackBufferState::RenderTarget)
                    : Result<void>::failure(std::move(*beginResult.try_error()));
    const bool valid = matches_error(transitionResult.try_error(), 94) && g_probeState.barrierCount == 0;
    Result<void> closeResult = objects->frameState->close_frame();
    Result<std::uint64_t> submitResult =
        closeResult ? execute_present_signal_for_probe(*objects->frameState)
                    : Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
    return submitResult && shutdown_probe_objects(objects) && valid && g_probeState.barrierCount == 0;
}

bool verify_d3d12_transition_unknown_target_for_probe(const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    Result<void> bindingResult = bind_probe_back_buffers(*objects, a_assertContext);

    if (!bindingResult)
    {
        static_cast<void>(shutdown_probe_objects(objects));
        return false;
    }

    Result<void> beginResult = objects->frameState->begin_frame(0);

    if (!beginResult)
    {
        static_cast<void>(shutdown_probe_objects(objects));
        return false;
    }

    Result<void> invalidTargetResult =
        objects->frameState->transition_back_buffer(0, D3d12BackBufferState::Unknown);
    bool valid = matches_error(invalidTargetResult.try_error(), 94) && g_probeState.barrierCount == 0 &&
                 objects->frameState->are_back_buffers_present();
    Result<void> renderTargetResult =
        objects->frameState->transition_back_buffer(0, D3d12BackBufferState::RenderTarget);
    Result<void> presentResult =
        renderTargetResult ? objects->frameState->transition_back_buffer(0, D3d12BackBufferState::Present)
                           : Result<void>::failure(std::move(*renderTargetResult.try_error()));
    Result<void> closeResult = presentResult ? objects->frameState->close_frame()
                                             : Result<void>::failure(std::move(*presentResult.try_error()));
    Result<std::uint64_t> submitResult =
        closeResult ? execute_present_signal_for_probe(*objects->frameState)
                    : Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
    valid = valid && submitResult && g_probeState.barrierCount == 2 && g_probeState.barriersAreValid &&
            objects->frameState->are_back_buffers_present();
    return shutdown_probe_objects(objects) && valid;
}

bool verify_d3d12_frame_wait_recovery_for_probe(const AssertContext &a_assertContext) noexcept
{
    constexpr std::array<ProbeWaitFaultMode, 5> faultModes = {
        ProbeWaitFaultMode::Registration, ProbeWaitFaultMode::Timeout,         ProbeWaitFaultMode::Failed,
        ProbeWaitFaultMode::Unexpected,   ProbeWaitFaultMode::EventIncomplete,
    };

    for (ProbeWaitFaultMode faultMode : faultModes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        g_probeState.nextFrameIndex = 0;
        Result<void> beginResult = objects->frameState->begin_frame(0);
        Result<void> closeResult = beginResult ? objects->frameState->close_frame()
                                               : Result<void>::failure(std::move(*beginResult.try_error()));
        Result<std::uint64_t> submitResult = closeResult
                                                 ? execute_present_signal_for_probe(*objects->frameState)
                                                 : Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));

        if (!submitResult)
        {
            return false;
        }

        Result<void> completedResult =
            objects->queueState.wait_for_fence(*submitResult.try_value(), D3d12FenceWaitPurpose::Reusable);

        if (!completedResult)
        {
            return false;
        }

        const std::uint32_t resetCountBeforeFault = g_probeState.allocatorResetCounts[0];
        const std::uint64_t reuseFenceBeforeFault = objects->frameState->frame_reuse_fence(0);
        const std::uint64_t lastSubmittedBeforeFault = objects->frameState->last_submitted_fence();
        g_probeState.waitFaultMode = faultMode;
        g_probeState.waitFaultActive = true;
        g_probeState.completedCallCount = 0;
        Result<void> faultBeginResult = objects->frameState->begin_frame(0);
        const bool faultValid =
            !faultBeginResult && objects->frameState->command_list_state() == D3d12CommandListState::Submitted &&
            objects->frameState->status() == D3d12FrameCommandStatus::Ready &&
            objects->frameState->is_accepting_frames() && g_probeState.allocatorResetCounts[0] == resetCountBeforeFault;
        const bool trackingValid = objects->frameState->frame_reuse_fence(0) == reuseFenceBeforeFault &&
                                   objects->frameState->last_submitted_fence() == lastSubmittedBeforeFault;
        g_probeState.waitFaultActive = false;
        g_probeState.waitFaultMode = ProbeWaitFaultMode::None;
        g_probeState.completedCallCount = 0;
        Result<void> retryBeginResult = objects->frameState->begin_frame(0);

        if (!faultValid || !trackingValid || !retryBeginResult ||
            g_probeState.allocatorResetCounts[0] != resetCountBeforeFault + 1)
        {
            return false;
        }

        Result<void> retryCloseResult = objects->frameState->close_frame();
        Result<std::uint64_t> retrySubmitResult =
            retryCloseResult ? execute_present_signal_for_probe(*objects->frameState)
                             : Result<std::uint64_t>::failure(std::move(*retryCloseResult.try_error()));

        if (!retrySubmitResult || !shutdown_probe_objects(objects))
        {
            return false;
        }
    }

    return true;
}

bool verify_d3d12_begin_frame_terminal_outcome_matrix_for_probe(const AssertContext &a_assertContext) noexcept
{
    constexpr std::array<ProbeWaitFaultMode, 4> terminalOutcomes = {
        ProbeWaitFaultMode::EventClose,
        ProbeWaitFaultMode::EventCreate,
        ProbeWaitFaultMode::Unavailable,
        ProbeWaitFaultMode::DeviceRemoved,
    };

    for (ProbeWaitFaultMode outcome : terminalOutcomes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<std::uint64_t> submitResult = submit_empty_frame_for_probe(*objects, 0);

        if (!submitResult ||
            !objects->queueState.wait_for_fence(*submitResult.try_value(), D3d12FenceWaitPurpose::Reusable))
        {
            return finish_probe_objects(std::move(objects), false);
        }

        const std::uint32_t resetCount = g_probeState.allocatorResetCounts[0];
        const std::uint64_t reuseFence = objects->frameState->frame_reuse_fence(0);
        const std::uint64_t lastSubmittedFence = objects->frameState->last_submitted_fence();
        g_probeState.waitFaultMode = outcome;
        g_probeState.waitFaultActive = true;
        g_probeState.completedCallCount = 0;
        Result<void> beginResult = objects->frameState->begin_frame(0);
        const D3d12FrameCommandStatus expectedStatus = outcome == ProbeWaitFaultMode::DeviceRemoved
                                                           ? D3d12FrameCommandStatus::DeviceRemoved
                                                           : D3d12FrameCommandStatus::Unavailable;
        bool recoveryStateValid = true;

        if (outcome == ProbeWaitFaultMode::EventClose)
        {
            recoveryStateValid = g_probeState.closeHandleCallCount == 1 && g_probeState.createEventCallCount == 1 &&
                                 g_probeState.initialEvent != nullptr && g_probeState.replacementEvent == nullptr;
        }
        else if (outcome == ProbeWaitFaultMode::EventCreate)
        {
            recoveryStateValid = g_probeState.closeHandleCallCount == 1 && g_probeState.createEventCallCount == 2 &&
                                 g_probeState.initialEvent != nullptr && g_probeState.replacementEvent == nullptr;
        }

        const bool valid = !beginResult && objects->frameState->status() == expectedStatus &&
                           !objects->frameState->is_accepting_frames() && objects->frameState->has_native_objects() &&
                           objects->frameState->command_list_state() == D3d12CommandListState::Submitted &&
                           objects->frameState->frame_reuse_fence(0) == reuseFence &&
                           objects->frameState->last_submitted_fence() == lastSubmittedFence &&
                           g_probeState.allocatorResetCounts[0] == resetCount && recoveryStateValid;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    Result<std::unique_ptr<ProbeObjects>> staleObjectsResult =
        create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

    if (!staleObjectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> staleObjects = std::move(*staleObjectsResult.try_value());
    Result<std::uint64_t> firstSubmit = submit_empty_frame_for_probe(*staleObjects, 0);

    if (!firstSubmit ||
        !staleObjects->queueState.wait_for_fence(*firstSubmit.try_value(), D3d12FenceWaitPurpose::Reusable))
    {
        return finish_probe_objects(std::move(staleObjects), false);
    }

    g_probeState.waitFaultMode = ProbeWaitFaultMode::Timeout;
    g_probeState.waitFaultActive = true;
    g_probeState.completedCallCount = 0;
    Result<void> recoveryResult = staleObjects->frameState->begin_frame(0);
    const bool replacementCreated = !recoveryResult &&
                                    staleObjects->frameState->status() == D3d12FrameCommandStatus::Ready &&
                                    g_probeState.createEventCallCount == 2 && g_probeState.replacementEvent != nullptr;
    disable_probe_faults();
    Result<void> retryBeginResult = staleObjects->frameState->begin_frame(0);
    Result<void> retryCloseResult = retryBeginResult ? staleObjects->frameState->close_frame()
                                                     : Result<void>::failure(std::move(*retryBeginResult.try_error()));
    g_probeState.waitFaultMode = ProbeWaitFaultMode::FollowupNoSignal;
    g_probeState.waitFaultActive = true;
    Result<std::uint64_t> unsignaledSubmit =
        retryCloseResult ? execute_present_signal_for_probe(*staleObjects->frameState)
                         : Result<std::uint64_t>::failure(std::move(*retryCloseResult.try_error()));
    const std::uint32_t resetCountBeforeFollowup = g_probeState.allocatorResetCounts[0];
    const std::uint32_t setEventCountBeforeFollowup = g_probeState.setEventCallCount;
    g_probeState.completedCallCount = 0;
    Result<void> followupResult = unsignaledSubmit ? staleObjects->frameState->begin_frame(0)
                                                   : Result<void>::failure(std::move(*unsignaledSubmit.try_error()));
    const bool staleValid = replacementCreated && !followupResult &&
                            staleObjects->frameState->status() == D3d12FrameCommandStatus::Unavailable &&
                            staleObjects->frameState->has_native_objects() &&
                            g_probeState.allocatorResetCounts[0] == resetCountBeforeFollowup &&
                            g_probeState.setEventCallCount == setEventCountBeforeFollowup + 1 &&
                            g_probeState.lastRegisteredEvent == g_probeState.replacementEvent;
    return finish_probe_objects(std::move(staleObjects), staleValid);
}

bool verify_d3d12_frame_reset_failed_terminal_matrix_for_probe(const AssertContext &a_assertContext) noexcept
{
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::AllocatorReset, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<void> resetResult = objects->frameState->begin_frame(0);
        const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        g_probeState.waitFaultMode = ProbeWaitFaultMode::Timeout;
        g_probeState.waitFaultActive = true;
        g_probeState.signalFaultActive = true;
        g_probeState.completedCallCount = 0;
        Result<void> shutdownResult = objects->frameState->shutdown();
        const bool valid = !resetResult && !shutdownResult && matches_error(shutdownResult.try_error(), 46) &&
                           objects->frameState->status() == D3d12FrameCommandStatus::Shutdown &&
                           !objects->frameState->has_native_objects() &&
                           objects->frameState->last_submitted_fence() == 1 &&
                           objects->frameState->frame_reuse_fence(0) == 0 &&
                           g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
                           g_probeState.commandListResetCount == listResetCount && g_probeState.executeCount == 0;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    constexpr std::array<ProbeWaitFaultMode, 5> completionModes = {
        ProbeWaitFaultMode::Registration, ProbeWaitFaultMode::Timeout,         ProbeWaitFaultMode::Failed,
        ProbeWaitFaultMode::Unexpected,   ProbeWaitFaultMode::EventIncomplete,
    };

    for (ProbeWaitFaultMode waitMode : completionModes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::AllocatorReset, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<void> resetResult = objects->frameState->begin_frame(0);
        const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        g_probeState.waitFaultMode = waitMode;
        g_probeState.waitFaultActive = true;
        g_probeState.completedCallCount = 0;
        Result<void> shutdownResult = objects->frameState->shutdown();
        const bool valid =
            !resetResult && !shutdownResult && objects->frameState->status() == D3d12FrameCommandStatus::Shutdown &&
            !objects->frameState->has_native_objects() && objects->frameState->last_submitted_fence() == 1 &&
            objects->frameState->frame_reuse_fence(0) == 0 &&
            g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
            g_probeState.commandListResetCount == listResetCount && g_probeState.executeCount == 0;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    constexpr std::array<ProbeWaitFaultMode, 2> retainedOutcomes = {
        ProbeWaitFaultMode::EventCreate,
        ProbeWaitFaultMode::Unavailable,
    };

    for (ProbeWaitFaultMode outcome : retainedOutcomes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::AllocatorReset, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<void> resetResult = objects->frameState->begin_frame(0);
        const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        g_probeState.waitFaultMode = outcome;
        g_probeState.waitFaultActive = true;
        g_probeState.completedCallCount = 0;
        Result<void> shutdownResult = objects->frameState->shutdown();
        const bool valid =
            !resetResult && !shutdownResult && objects->frameState->status() == D3d12FrameCommandStatus::Unavailable &&
            objects->frameState->has_native_objects() && g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
            g_probeState.commandListResetCount == listResetCount && g_probeState.executeCount == 0;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    for (bool signalFailure : {false, true})
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::AllocatorReset, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<void> resetResult = objects->frameState->begin_frame(0);
        const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        g_probeState.waitFaultMode = ProbeWaitFaultMode::DeviceRemoved;
        g_probeState.waitFaultActive = true;
        g_probeState.signalFaultActive = signalFailure;
        g_probeState.completedCallCount = 0;
        Result<void> shutdownResult = objects->frameState->shutdown();
        const std::uint64_t expectedTerminalFence = signalFailure ? 0 : 1;
        const bool valid = !resetResult && !shutdownResult && matches_error(shutdownResult.try_error(), 52) &&
                           objects->frameState->status() == D3d12FrameCommandStatus::DeviceRemoved &&
                           objects->frameState->has_native_objects() &&
                           objects->frameState->last_submitted_fence() == expectedTerminalFence &&
                           objects->frameState->frame_reuse_fence(0) == 0 &&
                           g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
                           g_probeState.commandListResetCount == listResetCount && g_probeState.executeCount == 0;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    constexpr std::array<ProbeFaultMode, 2> removalOrigins = {
        ProbeFaultMode::AllocatorReset,
        ProbeFaultMode::CommandListReset,
    };

    for (ProbeFaultMode origin : removalOrigins)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(origin, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        g_probeState.waitFaultMode = ProbeWaitFaultMode::DeviceRemoved;
        g_probeState.waitFaultActive = true;
        Result<void> resetResult = objects->frameState->begin_frame(0);
        const bool valid = !resetResult && matches_error(resetResult.try_error(), 52) &&
                           objects->frameState->status() == D3d12FrameCommandStatus::DeviceRemoved &&
                           !objects->frameState->is_accepting_frames() && objects->frameState->has_native_objects() &&
                           g_probeState.executeCount == 0;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    return true;
}

bool verify_d3d12_recording_close_failed_terminal_matrix_for_probe(const AssertContext &a_assertContext) noexcept
{
    constexpr std::array<ProbeWaitFaultMode, 4> terminalOutcomes = {
        ProbeWaitFaultMode::Timeout,
        ProbeWaitFaultMode::EventCreate,
        ProbeWaitFaultMode::Unavailable,
        ProbeWaitFaultMode::DeviceRemoved,
    };

    for (ProbeWaitFaultMode outcome : terminalOutcomes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::CommandListClose, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<void> beginResult = objects->frameState->begin_frame(0);

        if (!beginResult)
        {
            return finish_probe_objects(std::move(objects), false);
        }

        if (outcome == ProbeWaitFaultMode::DeviceRemoved)
        {
            g_probeState.waitFaultMode = outcome;
            g_probeState.waitFaultActive = true;
        }

        Result<void> closeResult = objects->frameState->close_frame();
        const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        const std::uint32_t closeCount = g_probeState.commandListCloseCount;

        if (outcome == ProbeWaitFaultMode::DeviceRemoved)
        {
            const bool valid = !closeResult && matches_error(closeResult.try_error(), 52) &&
                               objects->frameState->status() == D3d12FrameCommandStatus::DeviceRemoved &&
                               objects->frameState->has_native_objects() && g_probeState.executeCount == 0;

            if (!finish_probe_objects(std::move(objects), valid))
            {
                return false;
            }

            continue;
        }

        g_probeState.waitFaultMode = outcome;
        g_probeState.waitFaultActive = true;
        g_probeState.completedCallCount = 0;
        Result<void> shutdownResult = objects->frameState->shutdown();
        const D3d12FrameCommandStatus expectedStatus = outcome == ProbeWaitFaultMode::Timeout
                                                           ? D3d12FrameCommandStatus::Shutdown
                                                           : D3d12FrameCommandStatus::Unavailable;
        const bool valid =
            !closeResult && !shutdownResult && objects->frameState->status() == expectedStatus &&
            objects->frameState->has_native_objects() == (expectedStatus == D3d12FrameCommandStatus::Unavailable) &&
            g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
            g_probeState.commandListResetCount == listResetCount && g_probeState.commandListCloseCount == closeCount &&
            g_probeState.executeCount == 0;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    for (bool signalFailure : {false, true})
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::CommandListClose, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<void> beginResult = objects->frameState->begin_frame(0);
        Result<void> closeResult = beginResult ? objects->frameState->close_frame()
                                               : Result<void>::failure(std::move(*beginResult.try_error()));
        const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        const std::uint32_t closeCount = g_probeState.commandListCloseCount;
        g_probeState.waitFaultMode = ProbeWaitFaultMode::DeviceRemoved;
        g_probeState.waitFaultActive = true;
        g_probeState.signalFaultActive = signalFailure;
        g_probeState.completedCallCount = 0;
        Result<void> shutdownResult = objects->frameState->shutdown();
        const std::uint64_t expectedTerminalFence = signalFailure ? 0 : 1;
        const bool valid = !closeResult && !shutdownResult && matches_error(shutdownResult.try_error(), 52) &&
                           objects->frameState->status() == D3d12FrameCommandStatus::DeviceRemoved &&
                           objects->frameState->has_native_objects() &&
                           objects->frameState->last_submitted_fence() == expectedTerminalFence &&
                           g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
                           g_probeState.commandListResetCount == listResetCount &&
                           g_probeState.commandListCloseCount == closeCount && g_probeState.executeCount == 0;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    Result<std::unique_ptr<ProbeObjects>> signalObjectsResult =
        create_probe_objects(ProbeFaultMode::CommandListClose, false, a_assertContext);

    if (!signalObjectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> signalObjects = std::move(*signalObjectsResult.try_value());
    Result<void> beginResult = signalObjects->frameState->begin_frame(0);
    Result<void> closeResult = beginResult ? signalObjects->frameState->close_frame()
                                           : Result<void>::failure(std::move(*beginResult.try_error()));
    const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
    const std::uint32_t listResetCount = g_probeState.commandListResetCount;
    const std::uint32_t closeCount = g_probeState.commandListCloseCount;
    g_probeState.waitFaultMode = ProbeWaitFaultMode::Timeout;
    g_probeState.waitFaultActive = true;
    g_probeState.signalFaultActive = true;
    g_probeState.completedCallCount = 0;
    Result<void> shutdownResult = signalObjects->frameState->shutdown();
    const bool valid = !closeResult && !shutdownResult && matches_error(shutdownResult.try_error(), 46) &&
                       signalObjects->frameState->status() == D3d12FrameCommandStatus::Shutdown &&
                       !signalObjects->frameState->has_native_objects() &&
                       signalObjects->frameState->last_submitted_fence() == 1 &&
                       g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
                       g_probeState.commandListResetCount == listResetCount &&
                       g_probeState.commandListCloseCount == closeCount && g_probeState.executeCount == 0;
    return finish_probe_objects(std::move(signalObjects), valid);
}

bool verify_d3d12_frame_fence_exhaustion_matrix_for_probe(const AssertContext &a_assertContext) noexcept
{
    constexpr std::array<ProbeFaultMode, 3> faultModes = {
        ProbeFaultMode::None,
        ProbeFaultMode::DiscardCommandListReset,
        ProbeFaultMode::DiscardCommandListClose,
    };

    for (ProbeFaultMode faultMode : faultModes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(faultMode, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<void> beginResult = objects->frameState->begin_frame(0);
        Result<void> closeResult = beginResult ? objects->frameState->close_frame()
                                               : Result<void>::failure(std::move(*beginResult.try_error()));

        if (!closeResult)
        {
            return finish_probe_objects(std::move(objects), false);
        }

        objects->queueState.set_next_fence_value_for_test((std::numeric_limits<std::uint64_t>::max)());
        Result<void> executeResult = objects->frameState->execute_frame();
        const Error *executeError = executeResult.try_error();
        D3d12CommandListState expectedState = D3d12CommandListState::IdleClosed;
        bool secondaryValid = true;

        if (faultMode == ProbeFaultMode::DiscardCommandListReset)
        {
            expectedState = D3d12CommandListState::FrameResetFailed;
            secondaryValid = has_error_context(executeError, "Secondary Frame Command Error Code=Cue.RHI.D3D12/62");
        }
        else if (faultMode == ProbeFaultMode::DiscardCommandListClose)
        {
            expectedState = D3d12CommandListState::RecordingCloseFailed;
            secondaryValid = has_error_context(executeError, "Secondary Frame Command Error Code=Cue.RHI.D3D12/63");
        }

        const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        const std::uint32_t closeCount = g_probeState.commandListCloseCount;
        bool valid = !executeResult && matches_error(executeError, 45) && secondaryValid &&
                     objects->frameState->command_list_state() == expectedState &&
                     !objects->frameState->is_accepting_frames() && objects->frameState->has_native_objects() &&
                     objects->frameState->last_submitted_fence() == 0 && g_probeState.executeCount == 0;
        Result<void> frameShutdownResult = objects->frameState->shutdown();
        valid = valid && !frameShutdownResult && matches_error(frameShutdownResult.try_error(), 45) &&
                objects->frameState->status() == D3d12FrameCommandStatus::Shutdown &&
                !objects->frameState->has_native_objects() &&
                g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
                g_probeState.commandListResetCount == listResetCount &&
                g_probeState.commandListCloseCount == closeCount && g_probeState.executeCount == 0;
        Result<void> queueShutdownResult = objects->queueState.shutdown();
        valid = valid && !queueShutdownResult && matches_error(queueShutdownResult.try_error(), 45) &&
                objects->queueState.status() == D3d12QueueStateStatus::Shutdown &&
                !objects->queueState.has_native_objects();

        if (!valid)
        {
            return false;
        }
    }

    constexpr std::array<ProbeWaitFaultMode, 3> drainOutcomes = {
        ProbeWaitFaultMode::None,
        ProbeWaitFaultMode::Unavailable,
        ProbeWaitFaultMode::DeviceRemoved,
    };

    for (ProbeWaitFaultMode outcome : drainOutcomes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<std::uint64_t> submitResult = submit_empty_frame_for_probe(*objects, 0);
        Result<void> beginResult = submitResult ? objects->frameState->begin_frame(1)
                                                : Result<void>::failure(std::move(*submitResult.try_error()));
        Result<void> closeResult = beginResult ? objects->frameState->close_frame()
                                               : Result<void>::failure(std::move(*beginResult.try_error()));

        if (!closeResult)
        {
            return finish_probe_objects(std::move(objects), false);
        }

        objects->queueState.set_next_fence_value_for_test((std::numeric_limits<std::uint64_t>::max)());
        Result<void> executeResult = objects->frameState->execute_frame();
        const std::uint32_t frameZeroResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t frameOneResetCount = g_probeState.allocatorResetCounts[1];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        const std::uint32_t closeCount = g_probeState.commandListCloseCount;

        if (outcome != ProbeWaitFaultMode::None)
        {
            g_probeState.waitFaultMode = outcome;
            g_probeState.waitFaultActive = true;
            g_probeState.completedCallCount = 0;
        }

        Result<void> frameShutdownResult = objects->frameState->shutdown();
        const D3d12FrameCommandStatus expectedStatus =
            outcome == ProbeWaitFaultMode::None
                ? D3d12FrameCommandStatus::Shutdown
                : (outcome == ProbeWaitFaultMode::DeviceRemoved ? D3d12FrameCommandStatus::DeviceRemoved
                                                                : D3d12FrameCommandStatus::Unavailable);
        const std::int64_t expectedShutdownCode = outcome == ProbeWaitFaultMode::DeviceRemoved ? 52 : 45;
        const std::uint64_t expectedReuseFence = expectedStatus == D3d12FrameCommandStatus::Shutdown ? 0 : 1;
        bool valid =
            !executeResult && matches_error(executeResult.try_error(), 45) && !frameShutdownResult &&
            matches_error(frameShutdownResult.try_error(), expectedShutdownCode) &&
            objects->queueState.last_signaled_fence() == 1 && objects->frameState->status() == expectedStatus &&
            objects->frameState->last_submitted_fence() == 1 &&
            objects->frameState->frame_reuse_fence(0) == expectedReuseFence &&
            objects->frameState->has_native_objects() == (expectedStatus != D3d12FrameCommandStatus::Shutdown) &&
            g_probeState.allocatorResetCounts[0] == frameZeroResetCount &&
            g_probeState.allocatorResetCounts[1] == frameOneResetCount &&
            g_probeState.commandListResetCount == listResetCount && g_probeState.commandListCloseCount == closeCount &&
            g_probeState.executeCount == 1;

        if (outcome == ProbeWaitFaultMode::None)
        {
            Result<void> queueShutdownResult = objects->queueState.shutdown();
            valid = valid && !queueShutdownResult && matches_error(queueShutdownResult.try_error(), 45) &&
                    objects->queueState.status() == D3d12QueueStateStatus::Shutdown &&
                    !objects->queueState.has_native_objects();

            if (!valid)
            {
                return false;
            }
        }
        else if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    return true;
}

bool verify_d3d12_frame_signal_outcome_matrix_for_probe(const AssertContext &a_assertContext) noexcept
{
    constexpr std::array<ProbeWaitFaultMode, 3> outcomes = {
        ProbeWaitFaultMode::Timeout,
        ProbeWaitFaultMode::Unavailable,
        ProbeWaitFaultMode::DeviceRemoved,
    };

    for (ProbeWaitFaultMode outcome : outcomes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        g_probeState.nextFrameIndex = 0;
        Result<void> beginResult = objects->frameState->begin_frame(0);
        Result<void> closeResult = beginResult ? objects->frameState->close_frame()
                                               : Result<void>::failure(std::move(*beginResult.try_error()));
        Result<void> executeResult = closeResult ? objects->frameState->execute_frame()
                                                 : Result<void>::failure(std::move(*closeResult.try_error()));
        Result<void> presentResult = executeResult ? objects->frameState->mark_present_attempted()
                                                   : Result<void>::failure(std::move(*executeResult.try_error()));

        if (!presentResult)
        {
            return finish_probe_objects(std::move(objects), false);
        }

        g_probeState.waitFaultMode = outcome;
        g_probeState.waitFaultActive = true;
        g_probeState.signalFaultActive = true;
        g_probeState.completedCallCount = 0;
        Result<std::uint64_t> signalResult = objects->frameState->signal_frame();
        const bool completionProven = outcome == ProbeWaitFaultMode::Timeout;
        const D3d12FrameCommandStatus expectedStatus =
            completionProven ? D3d12FrameCommandStatus::Ready
                             : (outcome == ProbeWaitFaultMode::DeviceRemoved ? D3d12FrameCommandStatus::DeviceRemoved
                                                                             : D3d12FrameCommandStatus::Unavailable);
        const D3d12CommandListState expectedCommandState =
            completionProven ? D3d12CommandListState::Submitted : D3d12CommandListState::ExecutedUnfenced;
        const std::int64_t expectedErrorCode = outcome == ProbeWaitFaultMode::DeviceRemoved ? 52 : 46;
        const std::uint64_t expectedFenceValue = completionProven ? 1 : 0;
        Result<void> resizeSuspendResult = objects->frameState->suspend_for_resize();
        Result<void> resizeResumeResult = objects->frameState->resume_after_resize();
        const bool resizeRejectionValid = !completionProven ||
                                          (!resizeSuspendResult && matches_error(resizeSuspendResult.try_error(), 64) &&
                                           !resizeResumeResult && matches_error(resizeResumeResult.try_error(), 64));
        const bool valid = !signalResult && matches_error(signalResult.try_error(), expectedErrorCode) &&
                           objects->frameState->status() == expectedStatus &&
                           objects->frameState->command_list_state() == expectedCommandState &&
                           !objects->frameState->is_accepting_frames() && objects->frameState->has_native_objects() &&
                           objects->frameState->frame_reuse_fence(0) == expectedFenceValue &&
                           objects->frameState->last_submitted_fence() == expectedFenceValue &&
                           objects->queueState.last_signaled_fence() == expectedFenceValue &&
                           g_probeState.allocatorResetCounts[0] == 1 && g_probeState.commandListResetCount == 1 &&
                           g_probeState.executeCount == 1 && resizeRejectionValid;

        if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    return true;
}

bool verify_d3d12_frame_state_order_for_probe(D3d12FrameCommandOrderProbeMode a_mode,
                                              const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult =
        create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    bool valid = false;

    if (a_mode == D3d12FrameCommandOrderProbeMode::Begin)
    {
        Result<void> firstBeginResult = objects->frameState->begin_frame(0);

        if (!firstBeginResult)
        {
            return false;
        }

        Result<void> secondBeginResult = objects->frameState->begin_frame(1);
        valid = matches_error(secondBeginResult.try_error(), 60) && g_probeState.executeCount == 0;
        std::_Exit(valid ? 0 : 4);
    }

    if (a_mode == D3d12FrameCommandOrderProbeMode::ResizeSuspend)
    {
        Result<void> beginResult = objects->frameState->begin_frame(0);

        if (!beginResult)
        {
            return false;
        }

        Result<void> suspendResult = objects->frameState->suspend_for_resize();
        valid = matches_error(suspendResult.try_error(), 60) && objects->frameState->is_accepting_frames() &&
                objects->frameState->command_list_state() == D3d12CommandListState::Recording &&
                g_probeState.executeCount == 0;
        Result<void> closeResult = objects->frameState->close_frame();
        Result<std::uint64_t> submitResult =
            closeResult ? execute_present_signal_for_probe(*objects->frameState)
                        : Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
        return submitResult && shutdown_probe_objects(objects) && valid;
    }

    if (a_mode == D3d12FrameCommandOrderProbeMode::PresentState)
    {
        Result<void> bindingResult = bind_probe_back_buffers(*objects, a_assertContext);

        if (!bindingResult)
        {
            return false;
        }

        Result<void> beginResult = objects->frameState->begin_frame(0);
        Result<void> transitionResult =
            beginResult ? objects->frameState->transition_back_buffer(0, D3d12BackBufferState::RenderTarget)
                        : Result<void>::failure(std::move(*beginResult.try_error()));
        Result<void> closeResult = transitionResult ? objects->frameState->close_frame()
                                                    : Result<void>::failure(std::move(*transitionResult.try_error()));
        Result<void> executeResult = closeResult ? objects->frameState->execute_frame()
                                                 : Result<void>::failure(std::move(*closeResult.try_error()));
        Result<void> presentResult = executeResult ? objects->frameState->mark_present_attempted()
                                                   : Result<void>::failure(std::move(*executeResult.try_error()));
        valid = matches_error(presentResult.try_error(), 94) &&
                objects->frameState->command_list_state() == D3d12CommandListState::ExecutedUnfenced &&
                g_probeState.executeCount == 1;

        if (!valid)
        {
            std::_Exit(26);
        }

        Result<std::uint64_t> signalResult = objects->frameState->signal_frame();
        return signalResult && shutdown_probe_objects(objects) && valid;
    }

    if (a_mode == D3d12FrameCommandOrderProbeMode::Close)
    {
        Result<void> closeResult = objects->frameState->close_frame();
        valid = matches_error(closeResult.try_error(), 60) && g_probeState.executeCount == 0;
    }
    else
    {
        Result<void> executeResult = objects->frameState->execute_frame();
        valid = matches_error(executeResult.try_error(), 60) && g_probeState.executeCount == 0;
    }

    const std::array<std::uint32_t, k_d3d12FrameContextCount> resetCounts = g_probeState.allocatorResetCounts;
    const std::uint32_t listResetCount = g_probeState.commandListResetCount;
    const std::uint32_t listCloseCount = g_probeState.commandListCloseCount;
    const bool shutdownValid = shutdown_probe_objects(objects);
    return shutdownValid && valid && g_probeState.allocatorResetCounts == resetCounts &&
           g_probeState.commandListResetCount == listResetCount &&
           g_probeState.commandListCloseCount == listCloseCount && g_probeState.executeCount == 0;
}

bool verify_d3d12_frame_command_fault_for_probe(D3d12FrameCommandFaultProbeMode a_mode,
                                                const AssertContext &a_assertContext) noexcept
{
    ProbeFaultMode mode = ProbeFaultMode::AllocatorReset;

    if (a_mode == D3d12FrameCommandFaultProbeMode::CommandListReset)
    {
        mode = ProbeFaultMode::CommandListReset;
    }
    else if (a_mode == D3d12FrameCommandFaultProbeMode::CommandListClose)
    {
        mode = ProbeFaultMode::CommandListClose;
    }

    Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(mode, false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    Result<void> beginResult = objects->frameState->begin_frame(0);
    bool valid = false;

    if (a_mode == D3d12FrameCommandFaultProbeMode::AllocatorReset)
    {
        valid = matches_native_error(beginResult.try_error(), 61, k_probeFailure) &&
                objects->frameState->command_list_state() == D3d12CommandListState::FrameResetFailed &&
                !objects->frameState->is_accepting_frames() && objects->frameState->has_native_objects() &&
                g_probeState.commandListResetCount == 0 && g_probeState.executeCount == 0;
    }
    else if (a_mode == D3d12FrameCommandFaultProbeMode::CommandListReset)
    {
        valid = matches_native_error(beginResult.try_error(), 62, k_probeFailure) &&
                objects->frameState->command_list_state() == D3d12CommandListState::FrameResetFailed &&
                !objects->frameState->is_accepting_frames() && objects->frameState->has_native_objects() &&
                g_probeState.commandListResetCount == 1 && g_probeState.executeCount == 0;
    }
    else if (beginResult)
    {
        Result<void> closeResult = objects->frameState->close_frame();
        valid = matches_native_error(closeResult.try_error(), 63, k_probeFailure) &&
                objects->frameState->command_list_state() == D3d12CommandListState::RecordingCloseFailed &&
                !objects->frameState->is_accepting_frames() && objects->frameState->has_native_objects() &&
                g_probeState.executeCount == 0;
    }

    const std::array<std::uint32_t, k_d3d12FrameContextCount> resetCounts = g_probeState.allocatorResetCounts;
    const std::uint32_t listResetCount = g_probeState.commandListResetCount;
    const std::uint32_t listCloseCount = g_probeState.commandListCloseCount;
    const bool shutdownValid = shutdown_probe_objects(objects);
    return shutdownValid && valid && !objects->frameState->has_native_objects() &&
           g_probeState.allocatorResetCounts == resetCounts && g_probeState.commandListResetCount == listResetCount &&
           g_probeState.commandListCloseCount == listCloseCount && g_probeState.executeCount == 0;
}

bool verify_d3d12_resize_preparation_fault_matrix_for_probe(const AssertContext &a_assertContext) noexcept
{
    constexpr std::array<ProbeFaultMode, 3> faultModes = {
        ProbeFaultMode::AllocatorReset,
        ProbeFaultMode::CommandListReset,
        ProbeFaultMode::CommandListClose,
    };
    constexpr std::array<std::int64_t, 3> expectedErrorCodes = {61, 62, 63};
    constexpr std::array<D3d12CommandListState, 3> expectedStates = {
        D3d12CommandListState::FrameResetFailed,
        D3d12CommandListState::FrameResetFailed,
        D3d12CommandListState::RecordingCloseFailed,
    };

    for (std::size_t faultIndex = 0; faultIndex < faultModes.size(); ++faultIndex)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(faultModes[faultIndex], false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<void> resizeResult = objects->frameState->prepare_for_resize(0);
        const bool preparationValid =
            matches_native_error(resizeResult.try_error(), expectedErrorCodes[faultIndex], k_probeFailure) &&
            objects->frameState->status() == D3d12FrameCommandStatus::Ready &&
            objects->frameState->command_list_state() == expectedStates[faultIndex] &&
            !objects->frameState->is_accepting_frames() && objects->frameState->was_resize_gpu_idle_proven() &&
            objects->frameState->has_native_objects() && objects->frameState->frame_reuse_fence(0) == 0 &&
            objects->frameState->frame_reuse_fence(1) == 0 && g_probeState.executeCount == 0;
        Result<void> frameBeginResult = objects->frameState->begin_release_after_gpu_idle();
        bool allocatorReleased = false;

        if (objects->frameState->status() == D3d12FrameCommandStatus::CleanupPending)
        {
            Result<void> allocatorResult = objects->frameState->release_allocators_after_presentation_cleanup();
            allocatorReleased = static_cast<bool>(allocatorResult);
        }

        Result<void> queueShutdownResult = objects->queueState.shutdown();
        const bool cleanupValid = frameBeginResult && allocatorReleased && queueShutdownResult &&
                                  objects->frameState->status() == D3d12FrameCommandStatus::Shutdown &&
                                  !objects->frameState->has_native_objects() && g_probeState.executeCount == 0;

        if (!preparationValid || !cleanupValid)
        {
            return false;
        }
    }

    constexpr std::array<ProbeWaitFaultMode, 3> waitOutcomes = {
        ProbeWaitFaultMode::Timeout,
        ProbeWaitFaultMode::Unavailable,
        ProbeWaitFaultMode::DeviceRemoved,
    };

    for (ProbeWaitFaultMode outcome : waitOutcomes)
    {
        Result<std::unique_ptr<ProbeObjects>> objectsResult =
            create_probe_objects(ProbeFaultMode::None, false, a_assertContext);

        if (!objectsResult)
        {
            return false;
        }

        std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
        Result<std::uint64_t> submitResult = submit_empty_frame_for_probe(*objects, 0);

        if (!submitResult)
        {
            return finish_probe_objects(std::move(objects), false);
        }

        const std::uint32_t allocatorResetCount = g_probeState.allocatorResetCounts[0];
        const std::uint32_t listResetCount = g_probeState.commandListResetCount;
        const std::uint32_t listCloseCount = g_probeState.commandListCloseCount;
        g_probeState.waitFaultMode = outcome;
        g_probeState.waitFaultActive = true;
        g_probeState.completedCallCount = 0;
        Result<void> resizeResult = objects->frameState->prepare_for_resize(0);
        const bool completionProven = outcome == ProbeWaitFaultMode::Timeout;
        const D3d12FrameCommandStatus expectedStatus =
            completionProven ? D3d12FrameCommandStatus::Ready
                             : (outcome == ProbeWaitFaultMode::DeviceRemoved ? D3d12FrameCommandStatus::DeviceRemoved
                                                                             : D3d12FrameCommandStatus::Unavailable);
        bool valid = !resizeResult && objects->frameState->status() == expectedStatus &&
                     objects->frameState->command_list_state() == D3d12CommandListState::Submitted &&
                     !objects->frameState->is_accepting_frames() &&
                     objects->frameState->was_resize_gpu_idle_proven() == completionProven &&
                     objects->frameState->has_native_objects() &&
                     objects->frameState->frame_reuse_fence(0) == *submitResult.try_value() &&
                     objects->frameState->last_submitted_fence() == *submitResult.try_value() &&
                     g_probeState.allocatorResetCounts[0] == allocatorResetCount &&
                     g_probeState.commandListResetCount == listResetCount &&
                     g_probeState.commandListCloseCount == listCloseCount && g_probeState.executeCount == 1;

        if (completionProven)
        {
            disable_probe_faults();
            Result<void> frameBeginResult = objects->frameState->begin_release_after_gpu_idle();
            Result<void> allocatorResult = objects->frameState->release_allocators_after_presentation_cleanup();
            Result<void> queueShutdownResult = objects->queueState.shutdown();
            valid = valid && frameBeginResult && allocatorResult && queueShutdownResult &&
                    !objects->frameState->has_native_objects();

            if (!valid)
            {
                return false;
            }
        }
        else if (!finish_probe_objects(std::move(objects), valid))
        {
            return false;
        }
    }

    return true;
}
} // namespace cue
