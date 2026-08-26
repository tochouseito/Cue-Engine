#include <Cue/RHI/D3D12/TestSupport/D3d12QueueStateProbe.h>

#include "D3d12AdapterSelection.h"
#include "D3d12DeviceCreation.h"
#include "D3d12Diagnostics.h"
#include "D3d12QueueState.h"

#include <Cue/Foundation/Error.h>

#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace
{
thread_local bool g_forceInitialIncomplete = false;
thread_local bool g_usedEventWaitPath = false;

enum class NativeFaultMode
{
    None,
    WaitTimeoutCompletion,
    WaitRegistrationCompletion,
    WaitEventIncompleteCompletion,
    WaitFailedCompletion,
    WaitUnexpectedCompletion,
    WaitSentinelRefreshRemoval,
    FollowupNoSignal,
    WaitFailedUnavailable,
    EventCloseFailure,
    EventCreateFailure,
    SignalCompletion,
    SignalDeviceRemoved,
    SignalCompletionDeviceRemovedRace,
    SignalUnavailable,
    SignalEventCloseFailure,
    SignalEventCreateFailure,
    TerminalCloseFailure,
};

struct NativeFaultState final
{
    NativeFaultMode mode = NativeFaultMode::None;
    std::uint64_t targetFenceValue = 0;
    std::uint32_t completedCallCount = 0;
    std::uint32_t createEventCallCount = 0;
    std::uint32_t closeHandleCallCount = 0;
    std::uint32_t getLastErrorCallCount = 0;
    std::uint32_t removalReasonCallCount = 0;
    bool nativeFailurePending = false;
    bool getLastErrorOrderValid = true;
};

thread_local NativeFaultState g_faultState;

constexpr DWORD k_probeNativeError = 1234;
constexpr HRESULT k_probeSignalFailure = E_FAIL;

/// @brief Fault 検証後に Native 呼び出し差し替え状態を初期値へ戻す
void reset_fault_state(NativeFaultMode a_mode) noexcept
{
    g_faultState = {};
    g_faultState.mode = a_mode;
}

/// @brief Fault Injection 中の Native 呼び出し順を記録し、想定外呼び出しを検出する
void observe_native_call() noexcept
{
    if (g_faultState.nativeFailurePending)
    {
        g_faultState.getLastErrorOrderValid = false;
    }
}

/// @brief Native 呼び出し順を観測してから Command List 投入を実 Queue へ転送する
void execute_command_lists_for_fault(ID3D12CommandQueue *a_queue, UINT a_count,
                                     ID3D12CommandList *const *a_commandLists) noexcept
{
    observe_native_call();
    a_queue->ExecuteCommandLists(a_count, a_commandLists);
}

/// @brief Signal 対象値を記録し、選択した Fault Mode に応じて Signal 失敗または未送信成功を注入する
HRESULT signal_for_fault(ID3D12CommandQueue *a_queue, ID3D12Fence *a_fence, std::uint64_t a_value) noexcept
{
    observe_native_call();
    g_faultState.targetFenceValue = a_value;

    if (g_faultState.mode == NativeFaultMode::SignalCompletion ||
        g_faultState.mode == NativeFaultMode::SignalCompletionDeviceRemovedRace ||
        g_faultState.mode == NativeFaultMode::SignalEventCloseFailure ||
        g_faultState.mode == NativeFaultMode::SignalEventCreateFailure)
    {
        static_cast<void>(a_queue->Signal(a_fence, a_value));
        return k_probeSignalFailure;
    }

    if (g_faultState.mode == NativeFaultMode::SignalDeviceRemoved ||
        g_faultState.mode == NativeFaultMode::SignalUnavailable)
    {
        return k_probeSignalFailure;
    }

    if (g_faultState.mode == NativeFaultMode::FollowupNoSignal)
    {
        return S_OK;
    }

    return a_queue->Signal(a_fence, a_value);
}

/// @brief Fault Mode と呼び出し回数に応じて未完了値、完了値、Sentinel、または Native Fence 値を返す
std::uint64_t get_completed_value_for_fault(ID3D12Fence *a_fence) noexcept
{
    observe_native_call();
    ++g_faultState.completedCallCount;

    switch (g_faultState.mode)
    {
    case NativeFaultMode::SignalCompletionDeviceRemovedRace:
        return g_faultState.targetFenceValue;
    case NativeFaultMode::WaitTimeoutCompletion:
    case NativeFaultMode::WaitRegistrationCompletion:
    case NativeFaultMode::WaitFailedCompletion:
    case NativeFaultMode::WaitUnexpectedCompletion:
    case NativeFaultMode::EventCloseFailure:
    case NativeFaultMode::EventCreateFailure:
    case NativeFaultMode::SignalCompletion:
    case NativeFaultMode::SignalEventCloseFailure:
    case NativeFaultMode::SignalEventCreateFailure:
        return g_faultState.completedCallCount == 1 ? 0 : g_faultState.targetFenceValue;
    case NativeFaultMode::WaitEventIncompleteCompletion:
        return g_faultState.completedCallCount <= 2 ? 0 : g_faultState.targetFenceValue;
    case NativeFaultMode::WaitSentinelRefreshRemoval:
        return g_faultState.completedCallCount == 1 ? 0 : (std::numeric_limits<std::uint64_t>::max)();
    case NativeFaultMode::WaitFailedUnavailable:
    case NativeFaultMode::SignalDeviceRemoved:
    case NativeFaultMode::SignalUnavailable:
    case NativeFaultMode::FollowupNoSignal:
        return 0;
    default:
        return a_fence->GetCompletedValue();
    }
}

/// @brief Event 待機経路の使用を記録し、指定時は Fence Event 登録失敗を注入する
HRESULT set_event_on_completion_for_fault(ID3D12Fence *a_fence, std::uint64_t a_value, HANDLE a_event) noexcept
{
    observe_native_call();
    g_usedEventWaitPath = true;

    if (g_faultState.mode == NativeFaultMode::WaitRegistrationCompletion)
    {
        return E_FAIL;
    }

    return a_fence->SetEventOnCompletion(a_value, a_event);
}

/// @brief Fault Mode に対応する Timeout、Failure、Unexpected 結果を注入し、通常時は Win32 待機へ転送する
DWORD WINAPI wait_for_single_object_for_fault(HANDLE a_event, DWORD a_timeout)
{
    observe_native_call();

    switch (g_faultState.mode)
    {
    case NativeFaultMode::WaitTimeoutCompletion:
    case NativeFaultMode::EventCloseFailure:
    case NativeFaultMode::EventCreateFailure:
    case NativeFaultMode::SignalCompletion:
    case NativeFaultMode::SignalDeviceRemoved:
    case NativeFaultMode::SignalUnavailable:
    case NativeFaultMode::SignalEventCloseFailure:
    case NativeFaultMode::SignalEventCreateFailure:
    case NativeFaultMode::WaitSentinelRefreshRemoval:
        return WAIT_TIMEOUT;
    case NativeFaultMode::WaitEventIncompleteCompletion:
        return WAIT_OBJECT_0;
    case NativeFaultMode::WaitFailedUnavailable:
    case NativeFaultMode::WaitFailedCompletion:
        g_faultState.nativeFailurePending = true;
        return WAIT_FAILED;
    case NativeFaultMode::WaitUnexpectedCompletion:
        return WAIT_ABANDONED;
    default:
        return WaitForSingleObject(a_event, a_timeout);
    }
}

/// @brief Event 生成回数を記録し、交換用 Event 生成時に指定された Native 失敗を注入する
HANDLE WINAPI create_event_for_fault(LPSECURITY_ATTRIBUTES a_attributes, BOOL a_manualReset, BOOL a_initialState,
                                     LPCWSTR a_name)
{
    observe_native_call();
    ++g_faultState.createEventCallCount;

    if ((g_faultState.mode == NativeFaultMode::EventCreateFailure ||
         g_faultState.mode == NativeFaultMode::SignalEventCreateFailure) &&
        g_faultState.createEventCallCount == 2)
    {
        g_faultState.nativeFailurePending = true;
        return nullptr;
    }

    return CreateEventW(a_attributes, a_manualReset, a_initialState, a_name);
}

/// @brief Handle Close 回数を記録し、初回 Close へ指定された Native 失敗を注入する
BOOL WINAPI close_handle_for_fault(HANDLE a_handle)
{
    observe_native_call();
    ++g_faultState.closeHandleCallCount;

    if ((g_faultState.mode == NativeFaultMode::EventCloseFailure ||
         g_faultState.mode == NativeFaultMode::SignalEventCloseFailure ||
         g_faultState.mode == NativeFaultMode::TerminalCloseFailure) &&
        g_faultState.closeHandleCallCount == 1)
    {
        g_faultState.nativeFailurePending = true;
        return FALSE;
    }

    return CloseHandle(a_handle);
}

/// @brief Native 失敗直後にだけ GetLastError が呼ばれたか検証し、固定 Win32 Error Code を返す
DWORD WINAPI get_last_error_for_fault()
{
    ++g_faultState.getLastErrorCallCount;

    if (!g_faultState.nativeFailurePending)
    {
        g_faultState.getLastErrorOrderValid = false;
    }

    g_faultState.nativeFailurePending = false;
    return k_probeNativeError;
}

/// @brief Fault Mode と呼び出し回数に応じて Device Removal 発生時点を再現する
HRESULT get_device_removed_reason_for_fault(ID3D12Device *) noexcept
{
    observe_native_call();
    ++g_faultState.removalReasonCallCount;

    if (g_faultState.mode == NativeFaultMode::WaitSentinelRefreshRemoval)
    {
        return g_faultState.removalReasonCallCount == 1 ? S_OK : DXGI_ERROR_DEVICE_REMOVED;
    }

    if (g_faultState.mode == NativeFaultMode::SignalDeviceRemoved ||
        g_faultState.mode == NativeFaultMode::SignalCompletionDeviceRemovedRace)
    {
        return DXGI_ERROR_DEVICE_REMOVED;
    }

    return S_OK;
}

/// @brief Queue State の全 Native 境界を Fault Injection Callback へ差し替える関数 Table を返す
[[nodiscard]] cue::D3d12QueueNativeFunctions make_fault_functions() noexcept
{
    return {
        execute_command_lists_for_fault,
        signal_for_fault,
        get_completed_value_for_fault,
        set_event_on_completion_for_fault,
        wait_for_single_object_for_fault,
        create_event_for_fault,
        close_handle_for_fault,
        get_last_error_for_fault,
        get_device_removed_reason_for_fault,
    };
}

/// @brief 初回だけ未完了値を返して Event 待機経路へ誘導し、以後は Native Fence 値を返す
std::uint64_t get_completed_value_for_probe(ID3D12Fence *a_fence) noexcept
{
    if (g_forceInitialIncomplete)
    {
        g_forceInitialIncomplete = false;
        return 0;
    }

    return a_fence->GetCompletedValue();
}

/// @brief Event 待機経路の使用を記録してから Native Fence 登録へ転送する
HRESULT set_event_on_completion_for_probe(ID3D12Fence *a_fence, std::uint64_t a_value, HANDLE a_event) noexcept
{
    g_usedEventWaitPath = true;
    return a_fence->SetEventOnCompletion(a_value, a_event);
}

/// @brief D3D12 Queue State Probe で使用する WARP Device For Probe を生成し、呼び出し元へ返す
[[nodiscard]] cue::Result<Microsoft::WRL::ComPtr<ID3D12Device>> create_warp_device_for_probe(
    const cue::AssertContext &a_assertContext) noexcept
{
    cue::D3d12DiagnosticsStatus diagnostics = {};
    cue::Result<cue::D3d12AdapterSelection> selectionResult =
        cue::select_d3d12_adapter(cue::D3d12AdapterPolicy::Warp, diagnostics, a_assertContext);

    if (!selectionResult)
    {
        return cue::Result<Microsoft::WRL::ComPtr<ID3D12Device>>::failure(std::move(*selectionResult.try_error()));
    }

    cue::D3d12AdapterSelection selection = std::move(*selectionResult.try_value());
    return cue::create_d3d12_device(selection.adapter.Get(), selection.featureLevel, a_assertContext);
}

struct QueueProbeObjects final
{
    /// @brief Probe 用 Device と Queue State の所有権を同じ Lifecycle に束ねる
    QueueProbeObjects(Microsoft::WRL::ComPtr<ID3D12Device> a_device, cue::D3d12QueueState &&a_state) noexcept
        : device(std::move(a_device)), state(std::move(a_state))
    {
    }

    /// @brief QueueProbeObjects の一意所有を保つため Copy 構築を禁止する
    QueueProbeObjects(const QueueProbeObjects &) = delete;
    /// @brief QueueProbeObjects の一意所有を保つため Copy 代入を禁止する
    QueueProbeObjects &operator=(const QueueProbeObjects &) = delete;
    /// @brief Probe 用 Device と Queue State の一意所有を移動元から引き継ぐ
    QueueProbeObjects(QueueProbeObjects &&) noexcept = default;
    /// @brief QueueProbeObjects の所有状態を移動させないため Move 代入を禁止する
    QueueProbeObjects &operator=(QueueProbeObjects &&) noexcept = delete;
    /// @brief QueueProbeObjects が保持する Resource を所有権規則に従って破棄する
    ~QueueProbeObjects() noexcept = default;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    cue::D3d12QueueState state;
};

/// @brief D3D12 Queue State Probe で使用する Fault Objects を生成し、呼び出し元へ返す
[[nodiscard]] cue::Result<std::unique_ptr<QueueProbeObjects>> create_fault_objects(
    NativeFaultMode a_mode, const cue::AssertContext &a_assertContext) noexcept
{
    reset_fault_state(a_mode);
    cue::Result<Microsoft::WRL::ComPtr<ID3D12Device>> deviceResult = create_warp_device_for_probe(a_assertContext);

    if (!deviceResult)
    {
        return cue::Result<std::unique_ptr<QueueProbeObjects>>::failure(std::move(*deviceResult.try_error()));
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device = std::move(*deviceResult.try_value());
    cue::D3d12QueueNativeFunctions functions = make_fault_functions();
    cue::Result<cue::D3d12QueueState> stateResult =
        cue::create_d3d12_queue_state(device.Get(), 1, a_assertContext, functions);

    if (!stateResult)
    {
        return cue::Result<std::unique_ptr<QueueProbeObjects>>::failure(std::move(*stateResult.try_error()));
    }

    std::unique_ptr<QueueProbeObjects> objects =
        std::make_unique<QueueProbeObjects>(std::move(device), std::move(*stateResult.try_value()));
    return cue::Result<std::unique_ptr<QueueProbeObjects>>::success(std::move(objects));
}

/// @brief Error の抽象 Code と Native Error が期待値に一致するかを判定する
[[nodiscard]] bool matches_error(const cue::Error &a_error, std::int64_t a_code, std::string_view a_nativeDomain = {},
                                 std::int64_t a_nativeValue = 0) noexcept
{
    if (a_error.code().domain() != "Cue.RHI.D3D12" || a_error.code().value() != a_code)
    {
        return false;
    }

    if (a_nativeDomain.empty())
    {
        return a_error.try_native_error() == nullptr;
    }

    const cue::NativeError *nativeError = a_error.try_native_error();
    return nativeError != nullptr && nativeError->domain() == a_nativeDomain && nativeError->value() == a_nativeValue;
}

/// @brief Error または Cause が指定された診断 Context を持つかを返す
[[nodiscard]] bool has_context(const cue::Error &a_error, std::string_view a_message) noexcept
{
    for (const cue::ErrorContext &context : a_error.contexts())
    {
        if (context.message() == a_message)
        {
            return true;
        }
    }

    return false;
}

/// @brief 安全な解放を証明できない Native Owner を Process 終了まで保持する
void retain_until_process_exit(std::unique_ptr<QueueProbeObjects> &&a_objects) noexcept;

/// @brief Fault Probe の Cleanup 状態を確定し、検証結果を返す
[[nodiscard]] bool finish_fault_probe(std::unique_ptr<QueueProbeObjects> &&a_objects, bool a_valid) noexcept;

/// @brief Error または Cause が指定された診断 Context を持つかを返す
[[nodiscard]] bool has_context(const cue::ErrorCause &a_cause, std::string_view a_message) noexcept
{
    for (const cue::ErrorContext &context : a_cause.contexts())
    {
        if (context.message() == a_message)
        {
            return true;
        }
    }

    return false;
}

/// @brief D3D12 Queue State Probe の Wait Recovery Case が期待する契約を満たすか検証する
[[nodiscard]] bool verify_wait_recovery_case(NativeFaultMode a_mode, std::int64_t a_expectedCode,
                                             const cue::AssertContext &a_assertContext) noexcept
{
    cue::Result<std::unique_ptr<QueueProbeObjects>> objectsResult = create_fault_objects(a_mode, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<QueueProbeObjects> objects = std::move(*objectsResult.try_value());
    cue::Result<std::uint64_t> reservationResult = objects->state.reserve_fence_value();

    if (!reservationResult)
    {
        retain_until_process_exit(std::move(objects));
        return false;
    }

    const std::uint64_t fenceValue = *reservationResult.try_value();

    if (!objects->state.signal_reserved(fenceValue))
    {
        retain_until_process_exit(std::move(objects));
        return false;
    }

    cue::Result<void> waitResult = objects->state.wait_for_fence(fenceValue, cue::D3d12FenceWaitPurpose::Reusable);
    const cue::Error *waitError = waitResult.try_error();
    const bool recovered = waitError != nullptr && waitError->code().domain() == "Cue.RHI.D3D12" &&
                           waitError->code().value() == a_expectedCode &&
                           objects->state.was_last_wait_completion_proven() &&
                           objects->state.status() == cue::D3d12QueueStateStatus::Ready &&
                           g_faultState.closeHandleCallCount == 1 && g_faultState.createEventCallCount == 2;
    const bool nativeOrderValid = a_mode != NativeFaultMode::WaitFailedCompletion ||
                                  (waitError != nullptr && matches_error(*waitError, 49, "Win32", k_probeNativeError) &&
                                   g_faultState.getLastErrorOrderValid && g_faultState.getLastErrorCallCount == 1);
    reset_fault_state(NativeFaultMode::None);
    cue::Result<void> shutdownResult = objects->state.shutdown();
    return finish_fault_probe(std::move(objects), recovered && nativeOrderValid && shutdownResult);
}

void retain_until_process_exit(std::unique_ptr<QueueProbeObjects> &&a_objects) noexcept
{
    static_cast<void>(a_objects.release());
}

bool finish_fault_probe(std::unique_ptr<QueueProbeObjects> &&a_objects, bool a_valid) noexcept
{
    if (!a_objects)
    {
        return a_valid;
    }

    if (a_objects->state.status() == cue::D3d12QueueStateStatus::Ready)
    {
        reset_fault_state(NativeFaultMode::None);
        cue::Result<void> shutdownResult = a_objects->state.shutdown();
        a_valid = a_valid && static_cast<bool>(shutdownResult);
    }

    if (a_objects->state.status() == cue::D3d12QueueStateStatus::DeviceRemoved)
    {
        reset_fault_state(NativeFaultMode::None);
        cue::Result<void> releaseResult = a_objects->state.release_after_device_removed();
        a_valid = a_valid && static_cast<bool>(releaseResult);
    }

    if (a_objects->state.status() == cue::D3d12QueueStateStatus::Unavailable)
    {
        retain_until_process_exit(std::move(a_objects));
    }

    return a_valid;
}
} // namespace

namespace cue
{
Result<D3d12QueueStateProbeReport> probe_d3d12_queue_state(const AssertContext &a_assertContext) noexcept
{
    Result<Microsoft::WRL::ComPtr<ID3D12Device>> deviceResult = create_warp_device_for_probe(a_assertContext);

    if (!deviceResult)
    {
        return Result<D3d12QueueStateProbeReport>::failure(std::move(*deviceResult.try_error()));
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device = std::move(*deviceResult.try_value());
    D3d12QueueNativeFunctions functions = default_d3d12_queue_native_functions();
    functions.getCompletedValue = get_completed_value_for_probe;
    functions.setEventOnCompletion = set_event_on_completion_for_probe;
    Result<D3d12QueueState> stateResult = create_d3d12_queue_state(device.Get(), 5000, a_assertContext, functions);

    if (!stateResult)
    {
        return Result<D3d12QueueStateProbeReport>::failure(std::move(*stateResult.try_error()));
    }

    D3d12QueueState state = std::move(*stateResult.try_value());
    const std::uint64_t completedBeforeSignal = state.completed_value();
    Result<std::uint64_t> reservationResult = state.reserve_fence_value();

    if (!reservationResult)
    {
        return Result<D3d12QueueStateProbeReport>::failure(std::move(*reservationResult.try_error()));
    }

    const std::uint64_t reservedValue = *reservationResult.try_value();
    Result<void> signalResult = state.signal_reserved(reservedValue);

    if (!signalResult)
    {
        return Result<D3d12QueueStateProbeReport>::failure(std::move(*signalResult.try_error()));
    }

    g_forceInitialIncomplete = true;
    g_usedEventWaitPath = false;
    Result<void> waitResult = state.wait_for_fence(reservedValue, D3d12FenceWaitPurpose::Reusable);

    if (!waitResult)
    {
        return Result<D3d12QueueStateProbeReport>::failure(std::move(*waitResult.try_error()));
    }

    const std::uint64_t completedAfterWait = state.completed_value();
    D3d12QueueStateProbeReport report = {
        reservedValue, completedBeforeSignal, completedAfterWait, state.last_signaled_fence(), g_usedEventWaitPath,
    };
    Result<void> shutdownResult = state.shutdown();

    if (!shutdownResult)
    {
        return Result<D3d12QueueStateProbeReport>::failure(std::move(*shutdownResult.try_error()));
    }

    return Result<D3d12QueueStateProbeReport>::success(std::move(report));
}

bool verify_d3d12_fence_exhaustion_for_probe(const AssertContext &a_assertContext) noexcept
{
    Result<Microsoft::WRL::ComPtr<ID3D12Device>> deviceResult = create_warp_device_for_probe(a_assertContext);

    if (!deviceResult)
    {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device = std::move(*deviceResult.try_value());
    Result<D3d12QueueState> stateResult = create_d3d12_queue_state(device.Get(), 5000, a_assertContext);

    if (!stateResult)
    {
        return false;
    }

    D3d12QueueState state = std::move(*stateResult.try_value());
    constexpr std::uint64_t maximumSignalValue = (std::numeric_limits<std::uint64_t>::max)() - 1;
    state.set_next_fence_value_for_test(maximumSignalValue);
    Result<std::uint64_t> finalReservation = state.reserve_fence_value();

    if (!finalReservation || *finalReservation.try_value() != maximumSignalValue ||
        !state.signal_reserved(maximumSignalValue) ||
        !state.wait_for_fence(maximumSignalValue, D3d12FenceWaitPurpose::Reusable))
    {
        return false;
    }

    Result<std::uint64_t> exhaustedReservation = state.reserve_fence_value();
    const bool validExhaustion = !exhaustedReservation &&
                                 state.next_fence_value() == (std::numeric_limits<std::uint64_t>::max)() &&
                                 state.last_signaled_fence() == maximumSignalValue;
    Result<void> shutdownResult = state.shutdown();
    const Error *shutdownError = shutdownResult.try_error();
    return validExhaustion && shutdownError != nullptr && shutdownError->code().domain() == "Cue.RHI.D3D12" &&
           shutdownError->code().value() == 45 && state.status() == D3d12QueueStateStatus::Shutdown;
}

bool verify_d3d12_queue_fault_for_probe(D3d12QueueFaultProbeMode a_mode, const AssertContext &a_assertContext) noexcept
{
    if (a_mode == D3d12QueueFaultProbeMode::WaitRecoveryMatrix)
    {
        return verify_wait_recovery_case(NativeFaultMode::WaitTimeoutCompletion, 48, a_assertContext) &&
               verify_wait_recovery_case(NativeFaultMode::WaitRegistrationCompletion, 47, a_assertContext) &&
               verify_wait_recovery_case(NativeFaultMode::WaitEventIncompleteCompletion, 50, a_assertContext) &&
               verify_wait_recovery_case(NativeFaultMode::WaitFailedCompletion, 49, a_assertContext) &&
               verify_wait_recovery_case(NativeFaultMode::WaitUnexpectedCompletion, 49, a_assertContext);
    }

    NativeFaultMode nativeMode = NativeFaultMode::None;

    switch (a_mode)
    {
    case D3d12QueueFaultProbeMode::WaitSentinelRefreshRemoval:
        nativeMode = NativeFaultMode::WaitSentinelRefreshRemoval;
        break;
    case D3d12QueueFaultProbeMode::StaleEventFollowup:
        nativeMode = NativeFaultMode::WaitTimeoutCompletion;
        break;
    case D3d12QueueFaultProbeMode::WaitFailedUnavailable:
        nativeMode = NativeFaultMode::WaitFailedUnavailable;
        break;
    case D3d12QueueFaultProbeMode::EventCloseFailure:
        nativeMode = NativeFaultMode::EventCloseFailure;
        break;
    case D3d12QueueFaultProbeMode::EventCreateFailure:
        nativeMode = NativeFaultMode::EventCreateFailure;
        break;
    case D3d12QueueFaultProbeMode::SignalCompletion:
        nativeMode = NativeFaultMode::SignalCompletion;
        break;
    case D3d12QueueFaultProbeMode::SignalDeviceRemoved:
        nativeMode = NativeFaultMode::SignalDeviceRemoved;
        break;
    case D3d12QueueFaultProbeMode::SignalCompletionDeviceRemovedRace:
        nativeMode = NativeFaultMode::SignalCompletionDeviceRemovedRace;
        break;
    case D3d12QueueFaultProbeMode::SignalUnavailable:
        nativeMode = NativeFaultMode::SignalUnavailable;
        break;
    case D3d12QueueFaultProbeMode::SignalEventCloseFailure:
        nativeMode = NativeFaultMode::SignalEventCloseFailure;
        break;
    case D3d12QueueFaultProbeMode::SignalEventCreateFailure:
        nativeMode = NativeFaultMode::SignalEventCreateFailure;
        break;
    case D3d12QueueFaultProbeMode::TerminalSignalCompletion:
        nativeMode = NativeFaultMode::SignalCompletion;
        break;
    case D3d12QueueFaultProbeMode::TerminalSignalDeviceRemoved:
        nativeMode = NativeFaultMode::SignalDeviceRemoved;
        break;
    case D3d12QueueFaultProbeMode::TerminalSignalUnavailable:
        nativeMode = NativeFaultMode::SignalUnavailable;
        break;
    case D3d12QueueFaultProbeMode::TerminalCloseFailure:
        nativeMode = NativeFaultMode::TerminalCloseFailure;
        break;
    default:
        return false;
    }

    Result<std::unique_ptr<QueueProbeObjects>> objectsResult = create_fault_objects(nativeMode, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<QueueProbeObjects> objects = std::move(*objectsResult.try_value());

    if (a_mode == D3d12QueueFaultProbeMode::TerminalCloseFailure)
    {
        Result<void> shutdownResult = objects->state.shutdown();
        const Error *error = shutdownResult.try_error();
        const bool valid = error != nullptr && matches_error(*error, 51, "Win32", k_probeNativeError) &&
                           objects->state.status() == D3d12QueueStateStatus::Shutdown &&
                           !objects->state.has_gpu_objects() && objects->state.has_native_objects() &&
                           g_faultState.getLastErrorOrderValid && g_faultState.getLastErrorCallCount == 1;
        objects.reset();
        return valid && g_faultState.closeHandleCallCount == 2;
    }

    if (a_mode == D3d12QueueFaultProbeMode::TerminalSignalCompletion ||
        a_mode == D3d12QueueFaultProbeMode::TerminalSignalDeviceRemoved ||
        a_mode == D3d12QueueFaultProbeMode::TerminalSignalUnavailable)
    {
        Result<void> shutdownResult = objects->state.shutdown();
        const Error *error = shutdownResult.try_error();

        if (a_mode == D3d12QueueFaultProbeMode::TerminalSignalCompletion)
        {
            const bool valid =
                error != nullptr &&
                matches_error(*error, 46, "D3D12", static_cast<std::int64_t>(k_probeSignalFailure)) &&
                has_context(*error, "Secondary Queue Error Code=Cue.RHI.D3D12/48") &&
                objects->state.was_last_wait_completion_proven() && objects->state.last_signaled_fence() == 1 &&
                objects->state.status() == D3d12QueueStateStatus::Shutdown && !objects->state.has_native_objects();
            return finish_fault_probe(std::move(objects), valid);
        }

        if (a_mode == D3d12QueueFaultProbeMode::TerminalSignalDeviceRemoved)
        {
            const bool valid =
                error != nullptr &&
                matches_error(*error, 52, "D3D12", static_cast<std::int64_t>(DXGI_ERROR_DEVICE_REMOVED)) &&
                error->causes().size() == 1 && error->causes().front().code().value() == 46 &&
                has_context(error->causes().front(), "Secondary Queue Error Code=Cue.RHI.D3D12/48") &&
                objects->state.status() == D3d12QueueStateStatus::DeviceRemoved;
            reset_fault_state(NativeFaultMode::None);
            Result<void> releaseResult = objects->state.release_after_device_removed();
            return finish_fault_probe(std::move(objects), valid && releaseResult);
        }

        const bool valid =
            error != nullptr && matches_error(*error, 46, "D3D12", static_cast<std::int64_t>(k_probeSignalFailure)) &&
            has_context(*error, "Secondary Queue Error Code=Cue.RHI.D3D12/48") &&
            objects->state.status() == D3d12QueueStateStatus::Unavailable && objects->state.has_native_objects();
        return finish_fault_probe(std::move(objects), valid);
    }

    Result<std::uint64_t> reservationResult = objects->state.reserve_fence_value();

    if (!reservationResult)
    {
        return finish_fault_probe(std::move(objects), false);
    }

    const std::uint64_t fenceValue = *reservationResult.try_value();
    Result<void> signalResult = objects->state.signal_reserved(fenceValue);

    if (a_mode == D3d12QueueFaultProbeMode::WaitSentinelRefreshRemoval)
    {
        if (!signalResult)
        {
            return finish_fault_probe(std::move(objects), false);
        }

        Result<void> waitResult = objects->state.wait_for_fence(fenceValue, D3d12FenceWaitPurpose::BackendTerminal);
        const Error *error = waitResult.try_error();
        const bool valid = error != nullptr &&
                           matches_error(*error, 52, "D3D12", static_cast<std::int64_t>(DXGI_ERROR_DEVICE_REMOVED)) &&
                           error->causes().size() == 1 && error->causes().front().code().value() == 48 &&
                           g_faultState.removalReasonCallCount == 2 &&
                           objects->state.status() == D3d12QueueStateStatus::DeviceRemoved;
        return finish_fault_probe(std::move(objects), valid);
    }

    if (a_mode == D3d12QueueFaultProbeMode::StaleEventFollowup)
    {
        if (!signalResult)
        {
            return finish_fault_probe(std::move(objects), false);
        }

        Result<void> firstWaitResult = objects->state.wait_for_fence(fenceValue, D3d12FenceWaitPurpose::Reusable);

        if (firstWaitResult || objects->state.status() != D3d12QueueStateStatus::Ready)
        {
            return finish_fault_probe(std::move(objects), false);
        }

        g_faultState.mode = NativeFaultMode::FollowupNoSignal;
        g_faultState.completedCallCount = 0;
        g_usedEventWaitPath = false;
        Result<std::uint64_t> followupReservation = objects->state.reserve_fence_value();

        if (!followupReservation || !objects->state.signal_reserved(*followupReservation.try_value()))
        {
            return finish_fault_probe(std::move(objects), false);
        }

        Result<void> followupWait =
            objects->state.wait_for_fence(*followupReservation.try_value(), D3d12FenceWaitPurpose::Reusable);
        const Error *followupError = followupWait.try_error();
        const bool valid = followupError != nullptr && followupError->code().value() == 48 && g_usedEventWaitPath &&
                           objects->state.status() == D3d12QueueStateStatus::Unavailable;
        return finish_fault_probe(std::move(objects), valid);
    }

    if (a_mode == D3d12QueueFaultProbeMode::WaitFailedUnavailable ||
        a_mode == D3d12QueueFaultProbeMode::EventCloseFailure || a_mode == D3d12QueueFaultProbeMode::EventCreateFailure)
    {
        if (!signalResult)
        {
            return finish_fault_probe(std::move(objects), false);
        }

        Result<void> waitResult = objects->state.wait_for_fence(fenceValue, D3d12FenceWaitPurpose::Reusable);
        const Error *error = waitResult.try_error();
        bool valid = error != nullptr && objects->state.status() == D3d12QueueStateStatus::Unavailable &&
                     objects->state.has_native_objects() && g_faultState.getLastErrorOrderValid &&
                     g_faultState.getLastErrorCallCount == 1;

        if (a_mode == D3d12QueueFaultProbeMode::WaitFailedUnavailable)
        {
            valid = valid && matches_error(*error, 49, "Win32", k_probeNativeError) &&
                    !objects->state.was_last_wait_completion_proven();
        }
        else
        {
            valid = valid && matches_error(*error, 51, "Win32", k_probeNativeError) &&
                    objects->state.was_last_wait_completion_proven() && !error->causes().empty() &&
                    error->causes().front().code().value() == 48;
        }

        return finish_fault_probe(std::move(objects), valid);
    }

    if (signalResult)
    {
        return finish_fault_probe(std::move(objects), false);
    }

    Result<void> resolutionResult = objects->state.resolve_failed_signal(
        fenceValue, std::move(*signalResult.try_error()), D3d12FenceWaitPurpose::Reusable);
    const Error *error = resolutionResult.try_error();

    if (a_mode == D3d12QueueFaultProbeMode::SignalCompletion)
    {
        const bool valid =
            error != nullptr && matches_error(*error, 46, "D3D12", static_cast<std::int64_t>(k_probeSignalFailure)) &&
            has_context(*error, "Secondary Queue Error Code=Cue.RHI.D3D12/48") &&
            objects->state.was_last_wait_completion_proven() && objects->state.last_signaled_fence() == fenceValue &&
            objects->state.status() == D3d12QueueStateStatus::Ready;
        return finish_fault_probe(std::move(objects), valid);
    }

    if (a_mode == D3d12QueueFaultProbeMode::SignalEventCloseFailure ||
        a_mode == D3d12QueueFaultProbeMode::SignalEventCreateFailure)
    {
        const bool valid = error != nullptr && matches_error(*error, 51, "Win32", k_probeNativeError) &&
                           error->causes().size() == 1 && error->causes().front().code().value() == 46 &&
                           has_context(error->causes().front(), "Secondary Queue Error Code=Cue.RHI.D3D12/48") &&
                           objects->state.status() == D3d12QueueStateStatus::Unavailable &&
                           objects->state.has_native_objects() && objects->state.was_last_wait_completion_proven() &&
                           g_faultState.getLastErrorOrderValid && g_faultState.getLastErrorCallCount == 1;
        return finish_fault_probe(std::move(objects), valid);
    }

    if (a_mode == D3d12QueueFaultProbeMode::SignalDeviceRemoved ||
        a_mode == D3d12QueueFaultProbeMode::SignalCompletionDeviceRemovedRace)
    {
        const bool valid = error != nullptr &&
                           matches_error(*error, 52, "D3D12", static_cast<std::int64_t>(DXGI_ERROR_DEVICE_REMOVED)) &&
                           error->causes().size() == 1 && error->causes().front().code().value() == 46 &&
                           (a_mode == D3d12QueueFaultProbeMode::SignalCompletionDeviceRemovedRace ||
                            has_context(error->causes().front(), "Secondary Queue Error Code=Cue.RHI.D3D12/48")) &&
                           objects->state.last_signaled_fence() == 0 &&
                           objects->state.status() == D3d12QueueStateStatus::DeviceRemoved;
        reset_fault_state(NativeFaultMode::None);
        Result<void> releaseResult = objects->state.release_after_device_removed();
        return finish_fault_probe(std::move(objects), valid && releaseResult);
    }

    const bool valid = error != nullptr &&
                       matches_error(*error, 46, "D3D12", static_cast<std::int64_t>(k_probeSignalFailure)) &&
                       has_context(*error, "Secondary Queue Error Code=Cue.RHI.D3D12/48") &&
                       objects->state.status() == D3d12QueueStateStatus::Unavailable &&
                       objects->state.has_native_objects() && !objects->state.was_last_wait_completion_proven();

    return finish_fault_probe(std::move(objects), valid);
}
} // namespace cue
