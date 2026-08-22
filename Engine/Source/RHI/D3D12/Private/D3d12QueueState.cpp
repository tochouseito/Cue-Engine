#include "D3d12QueueState.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::int64_t k_queueCreationFailed = 40;
constexpr std::int64_t k_queueNameFailed = 41;
constexpr std::int64_t k_fenceCreationFailed = 42;
constexpr std::int64_t k_fenceNameFailed = 43;
constexpr std::int64_t k_eventCreationFailed = 44;
constexpr std::int64_t k_fenceValueExhausted = 45;
constexpr std::int64_t k_fenceSignalFailed = 46;
constexpr std::int64_t k_fenceWaitRegistrationFailed = 47;
constexpr std::int64_t k_fenceWaitTimeout = 48;
constexpr std::int64_t k_fenceWaitPrimitiveFailed = 49;
constexpr std::int64_t k_fenceWaitIncomplete = 50;
constexpr std::int64_t k_fenceWaitEventRecoveryFailed = 51;
constexpr std::int64_t k_deviceRemoved = 52;
constexpr std::int64_t k_invalidFenceReservation = 53;
constexpr std::uint64_t k_deviceRemovedCompletedValue = (std::numeric_limits<std::uint64_t>::max)();
constexpr std::uint64_t k_maximumSignalValue = k_deviceRemovedCompletedValue - 1;

[[nodiscard]] cue::ErrorCode make_code(const cue::AssertContext &a_context, std::int64_t a_value) noexcept
{
    return cue::ErrorCode::create(a_context.fatal_handler(), "Cue.RHI.D3D12", a_value);
}

[[nodiscard]] cue::Error make_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                    std::string_view a_summary) noexcept
{
    return cue::Error::create(a_context.fatal_handler(), make_code(a_context, a_code), a_summary);
}

[[nodiscard]] cue::Error make_native_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                           std::string_view a_summary, std::string_view a_nativeDomain,
                                           std::int64_t a_nativeCode) noexcept
{
    cue::NativeError nativeError = cue::NativeError::create(a_context.fatal_handler(), a_nativeDomain, a_nativeCode);
    return cue::Error::create(a_context.fatal_handler(), make_code(a_context, a_code), a_summary,
                              std::move(nativeError));
}

[[nodiscard]] cue::Error make_device_removed_error(const cue::AssertContext &a_context, HRESULT a_reason,
                                                   cue::Error &&a_cause) noexcept
{
    cue::NativeError nativeError =
        cue::NativeError::create(a_context.fatal_handler(), "D3D12", static_cast<std::int64_t>(a_reason));
    return cue::Error::reclassify(a_context.fatal_handler(), make_code(a_context, k_deviceRemoved),
                                  "D3D12 Device was removed", std::move(nativeError), std::move(a_cause));
}

void add_error_identity_context(cue::Error &a_primaryError, std::string_view a_label, const cue::ErrorCode &a_code,
                                const cue::NativeError *a_nativeError,
                                const cue::AssertContext &a_assertContext) noexcept
{
    try
    {
        std::string codeContext(a_label);
        codeContext.append(" Code=");
        codeContext.append(a_code.domain());
        codeContext.push_back('/');
        codeContext.append(std::to_string(a_code.value()));
        a_primaryError.add_context(a_assertContext.fatal_handler(), codeContext);

        if (a_nativeError != nullptr)
        {
            std::string nativeContext(a_label);
            nativeContext.append(" NativeError=");
            nativeContext.append(a_nativeError->domain());
            nativeContext.push_back('/');
            nativeContext.append(std::to_string(a_nativeError->value()));
            a_primaryError.add_context(a_assertContext.fatal_handler(), nativeContext);
        }
    }
    catch (...)
    {
        a_assertContext.fatal_handler().terminate("D3D12 Queue Error context allocation failed");
    }
}

void add_secondary_error_context(cue::Error &a_primaryError, const cue::Error &a_secondaryError,
                                 std::string_view a_context, const cue::AssertContext &a_assertContext) noexcept
{
    a_primaryError.add_context(a_assertContext.fatal_handler(), a_context);
    a_primaryError.add_context(a_assertContext.fatal_handler(), a_secondaryError.summary());
    add_error_identity_context(a_primaryError, "Secondary Queue Error", a_secondaryError.code(),
                               a_secondaryError.try_native_error(), a_assertContext);

    for (const cue::ErrorContext &context : a_secondaryError.contexts())
    {
        a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
    }

    for (const cue::ErrorCause &cause : a_secondaryError.causes())
    {
        a_primaryError.add_context(a_assertContext.fatal_handler(), "Secondary Queue Error cause");
        a_primaryError.add_context(a_assertContext.fatal_handler(), cause.summary());
        add_error_identity_context(a_primaryError, "Secondary Queue Error cause", cause.code(),
                                   cause.try_native_error(), a_assertContext);

        for (const cue::ErrorContext &context : cause.contexts())
        {
            a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
        }
    }
}

void add_secondary_cause_context(cue::Error &a_primaryError, const cue::ErrorCause &a_secondaryCause,
                                 std::string_view a_context, const cue::AssertContext &a_assertContext) noexcept
{
    a_primaryError.add_context(a_assertContext.fatal_handler(), a_context);
    a_primaryError.add_context(a_assertContext.fatal_handler(), a_secondaryCause.summary());
    add_error_identity_context(a_primaryError, "Secondary Queue Error", a_secondaryCause.code(),
                               a_secondaryCause.try_native_error(), a_assertContext);

    for (const cue::ErrorContext &context : a_secondaryCause.contexts())
    {
        a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
    }
}

void retain_secondary_error(cue::Error &a_primaryError, cue::Result<void> &a_secondaryResult,
                            std::string_view a_context, const cue::AssertContext &a_assertContext) noexcept
{
    if (!a_secondaryResult)
    {
        add_secondary_error_context(a_primaryError, *a_secondaryResult.try_error(), a_context, a_assertContext);
    }
}

void execute_command_lists(ID3D12CommandQueue *a_queue, UINT a_count, ID3D12CommandList *const *a_commandLists) noexcept
{
    a_queue->ExecuteCommandLists(a_count, a_commandLists);
}

HRESULT signal_queue(ID3D12CommandQueue *a_queue, ID3D12Fence *a_fence, std::uint64_t a_value) noexcept
{
    return a_queue->Signal(a_fence, a_value);
}

std::uint64_t get_completed_value(ID3D12Fence *a_fence) noexcept
{
    return a_fence->GetCompletedValue();
}

HRESULT set_event_on_completion(ID3D12Fence *a_fence, std::uint64_t a_value, HANDLE a_event) noexcept
{
    return a_fence->SetEventOnCompletion(a_value, a_event);
}

HRESULT get_device_removed_reason(ID3D12Device *a_device) noexcept
{
    return a_device->GetDeviceRemovedReason();
}
} // namespace

namespace cue
{
const D3d12QueueNativeFunctions &default_d3d12_queue_native_functions() noexcept
{
    static const D3d12QueueNativeFunctions functions = {
        execute_command_lists, signal_queue, get_completed_value, set_event_on_completion,   WaitForSingleObject,
        CreateEventW,          CloseHandle,  GetLastError,        get_device_removed_reason,
    };
    return functions;
}

D3d12FenceEvent::D3d12FenceEvent() noexcept : m_handle(nullptr), m_closeHandle(nullptr)
{
}

D3d12FenceEvent::D3d12FenceEvent(HANDLE a_handle, BOOL(WINAPI *a_closeHandle)(HANDLE)) noexcept
    : m_handle(a_handle), m_closeHandle(a_closeHandle)
{
}

D3d12FenceEvent::D3d12FenceEvent(D3d12FenceEvent &&a_other) noexcept
    : m_handle(a_other.m_handle), m_closeHandle(a_other.m_closeHandle)
{
    a_other.m_handle = nullptr;
    a_other.m_closeHandle = nullptr;
}

void D3d12FenceEvent::take_from(D3d12FenceEvent &&a_other) noexcept
{
    m_handle = a_other.m_handle;
    m_closeHandle = a_other.m_closeHandle;
    a_other.m_handle = nullptr;
    a_other.m_closeHandle = nullptr;
}

D3d12FenceEvent::~D3d12FenceEvent() noexcept
{
    if (m_handle != nullptr && m_closeHandle != nullptr)
    {
        static_cast<void>(m_closeHandle(m_handle));
    }
}

HANDLE D3d12FenceEvent::get() const noexcept
{
    return m_handle;
}

bool D3d12FenceEvent::is_open() const noexcept
{
    return m_handle != nullptr;
}

void D3d12FenceEvent::reset(HANDLE a_handle, BOOL(WINAPI *a_closeHandle)(HANDLE)) noexcept
{
    m_handle = a_handle;
    m_closeHandle = a_closeHandle;
}

void D3d12FenceEvent::mark_closed() noexcept
{
    m_handle = nullptr;
}

D3d12QueueState::D3d12QueueState(Microsoft::WRL::ComPtr<ID3D12CommandQueue> a_queue,
                                 Microsoft::WRL::ComPtr<ID3D12Fence> a_fence, D3d12FenceEvent &&a_event,
                                 ID3D12Device *a_device, std::uint32_t a_waitTimeoutMilliseconds,
                                 const D3d12QueueNativeFunctions &a_functions,
                                 const AssertContext &a_assertContext) noexcept
    : m_queue(std::move(a_queue)), m_fence(std::move(a_fence)), m_event(std::move(a_event)), m_device(a_device),
      m_nextFenceValue(1), m_lastAttemptedFence(0), m_lastSignaledFence(0),
      m_waitTimeoutMilliseconds(a_waitTimeoutMilliseconds), m_functions(a_functions), m_assertContext(&a_assertContext),
      m_status(D3d12QueueStateStatus::Ready), m_lastWaitCompletionProven(false)
{
}

D3d12QueueState &D3d12QueueState::operator=(D3d12QueueState &&a_other) noexcept
{
    if (this != &a_other)
    {
        if (has_native_objects())
        {
            m_assertContext->fatal_handler().terminate("Active D3D12 Queue ownership cannot be move-assigned");
        }

        m_queue = std::move(a_other.m_queue);
        m_fence = std::move(a_other.m_fence);
        m_event.take_from(std::move(a_other.m_event));
        m_device = a_other.m_device;
        m_nextFenceValue = a_other.m_nextFenceValue;
        m_lastAttemptedFence = a_other.m_lastAttemptedFence;
        m_lastSignaledFence = a_other.m_lastSignaledFence;
        m_waitTimeoutMilliseconds = a_other.m_waitTimeoutMilliseconds;
        m_functions = a_other.m_functions;
        m_assertContext = a_other.m_assertContext;
        m_status = a_other.m_status;
        m_lastWaitCompletionProven = a_other.m_lastWaitCompletionProven;
    }

    return *this;
}

Result<std::uint64_t> D3d12QueueState::reserve_fence_value() noexcept
{
    CUE_ASSERT(*m_assertContext, m_status == D3d12QueueStateStatus::Ready,
               "Fence values can only be reserved while the Queue is ready");
    CUE_ASSERT(*m_assertContext, m_lastAttemptedFence == m_nextFenceValue - 1,
               "Only one D3D12 Fence reservation may be outstanding");

    if (m_nextFenceValue > k_maximumSignalValue)
    {
        return Result<std::uint64_t>::failure(
            make_error(*m_assertContext, k_fenceValueExhausted, "D3D12 Fence value range is exhausted"));
    }

    std::uint64_t reservedValue = m_nextFenceValue;
    ++m_nextFenceValue;
    return Result<std::uint64_t>::success(std::move(reservedValue));
}

void D3d12QueueState::execute_command_list(ID3D12CommandList *a_commandList) noexcept
{
    CUE_ASSERT(*m_assertContext, m_status == D3d12QueueStateStatus::Ready,
               "D3D12 Command List execution requires a ready Queue");
    CUE_ASSERT(*m_assertContext, a_commandList != nullptr, "D3D12 Command List execution requires a Command List");
    ID3D12CommandList *commandLists[] = {a_commandList};
    m_functions.executeCommandLists(m_queue.Get(), 1, commandLists);
}

Result<void> D3d12QueueState::signal_reserved(std::uint64_t a_fenceValue) noexcept
{
    CUE_ASSERT(*m_assertContext, m_status == D3d12QueueStateStatus::Ready, "D3D12 Queue Signal requires a ready Queue");

    if (a_fenceValue == 0 || a_fenceValue >= m_nextFenceValue || a_fenceValue <= m_lastAttemptedFence ||
        a_fenceValue != m_nextFenceValue - 1 || a_fenceValue == k_deviceRemovedCompletedValue)
    {
        return Result<void>::failure(
            make_error(*m_assertContext, k_invalidFenceReservation, "D3D12 Fence reservation is invalid"));
    }

    m_lastAttemptedFence = a_fenceValue;
    const HRESULT signalResult = m_functions.signal(m_queue.Get(), m_fence.Get(), a_fenceValue);

    if (FAILED(signalResult))
    {
        return Result<void>::failure(make_native_error(*m_assertContext, k_fenceSignalFailed,
                                                       "D3D12 Queue Fence Signal failed", "D3D12",
                                                       static_cast<std::int64_t>(signalResult)));
    }

    if (a_fenceValue > m_lastSignaledFence)
    {
        m_lastSignaledFence = a_fenceValue;
    }

    return Result<void>::success();
}

Result<void> D3d12QueueState::reclassify_device_failure(Error &&a_error) noexcept
{
    const HRESULT removalReason = m_functions.getDeviceRemovedReason(m_device);

    if (FAILED(removalReason))
    {
        m_status = D3d12QueueStateStatus::DeviceRemoved;
        return Result<void>::failure(make_device_removed_error(*m_assertContext, removalReason, std::move(a_error)));
    }

    return Result<void>::failure(std::move(a_error));
}

bool D3d12QueueState::refresh_device_removed_status() noexcept
{
    CUE_ASSERT(*m_assertContext,
               m_status == D3d12QueueStateStatus::Ready || m_status == D3d12QueueStateStatus::DeviceRemoved,
               "D3D12 Device Removal refresh requires a live Queue");

    if (m_status == D3d12QueueStateStatus::DeviceRemoved)
    {
        return true;
    }

    const HRESULT removalReason = m_functions.getDeviceRemovedReason(m_device);

    if (FAILED(removalReason))
    {
        m_status = D3d12QueueStateStatus::DeviceRemoved;
        return true;
    }

    return false;
}

Result<void> D3d12QueueState::resolve_failed_signal(std::uint64_t a_fenceValue, Error &&a_signalError,
                                                    D3d12FenceWaitPurpose a_purpose) noexcept
{
    CUE_ASSERT(*m_assertContext, m_status == D3d12QueueStateStatus::Ready,
               "D3D12 Signal failure resolution requires a ready Queue");
    CUE_ASSERT(*m_assertContext, a_fenceValue == m_lastAttemptedFence,
               "D3D12 Signal failure resolution requires the last attempted Fence");

    Result<void> waitResult = wait_for_fence(a_fenceValue, D3d12FenceWaitPurpose::BackendTerminal);

    if (m_status == D3d12QueueStateStatus::DeviceRemoved)
    {
        const Error *deviceRemovedError = waitResult.try_error();
        const NativeError *removalNativeError = deviceRemovedError->try_native_error();

        if (!deviceRemovedError->causes().empty())
        {
            add_secondary_cause_context(a_signalError, deviceRemovedError->causes().front(),
                                        "D3D12 Fence Wait also failed before Device Removal", *m_assertContext);
        }

        NativeError nativeError = NativeError::create(m_assertContext->fatal_handler(), removalNativeError->domain(),
                                                      removalNativeError->value());
        return Result<void>::failure(
            Error::reclassify(m_assertContext->fatal_handler(), make_code(*m_assertContext, k_deviceRemoved),
                              "D3D12 Device was removed", std::move(nativeError), std::move(a_signalError)));
    }

    const HRESULT removalReason = m_functions.getDeviceRemovedReason(m_device);

    if (FAILED(removalReason))
    {
        retain_secondary_error(a_signalError, waitResult,
                               "D3D12 Fence Wait also reported an Error before Device Removal", *m_assertContext);
        m_status = D3d12QueueStateStatus::DeviceRemoved;
        return Result<void>::failure(
            make_device_removed_error(*m_assertContext, removalReason, std::move(a_signalError)));
    }

    if (waitResult || m_lastWaitCompletionProven)
    {
        if (a_fenceValue > m_lastSignaledFence)
        {
            m_lastSignaledFence = a_fenceValue;
        }

        const bool waitReportedError = !waitResult;
        retain_secondary_error(a_signalError, waitResult,
                               "D3D12 Fence Wait also reported an Error after Signal completion was proven",
                               *m_assertContext);

        if (a_purpose == D3d12FenceWaitPurpose::Reusable && waitReportedError)
        {
            return replace_wait_event(std::move(a_signalError));
        }

        return Result<void>::failure(std::move(a_signalError));
    }

    retain_secondary_error(a_signalError, waitResult, "D3D12 Fence Wait also failed without proven Signal completion",
                           *m_assertContext);
    return Result<void>::failure(std::move(a_signalError));
}

std::uint64_t D3d12QueueState::completed_value() noexcept
{
    return m_functions.getCompletedValue(m_fence.Get());
}

Result<void> D3d12QueueState::wait_for_fence(std::uint64_t a_fenceValue, D3d12FenceWaitPurpose a_purpose) noexcept
{
    CUE_ASSERT(*m_assertContext, m_status == D3d12QueueStateStatus::Ready, "D3D12 Fence Wait requires a ready Queue");
    m_lastWaitCompletionProven = false;

    if (a_fenceValue == 0)
    {
        m_lastWaitCompletionProven = true;
        return Result<void>::success();
    }

    const std::uint64_t initialCompletedValue = completed_value();

    if (initialCompletedValue == k_deviceRemovedCompletedValue)
    {
        Error cause = make_error(*m_assertContext, k_fenceWaitIncomplete, "D3D12 Fence reported device removal");
        const HRESULT removalReason = m_functions.getDeviceRemovedReason(m_device);
        m_status = D3d12QueueStateStatus::DeviceRemoved;
        return Result<void>::failure(make_device_removed_error(*m_assertContext, removalReason, std::move(cause)));
    }

    if (initialCompletedValue >= a_fenceValue)
    {
        m_lastWaitCompletionProven = true;
        return Result<void>::success();
    }

    const HRESULT registrationResult = m_functions.setEventOnCompletion(m_fence.Get(), a_fenceValue, m_event.get());

    if (FAILED(registrationResult))
    {
        return resolve_abnormal_wait(make_native_error(*m_assertContext, k_fenceWaitRegistrationFailed,
                                                       "D3D12 Fence Event registration failed", "D3D12",
                                                       static_cast<std::int64_t>(registrationResult)),
                                     a_fenceValue, a_purpose);
    }

    const DWORD waitResult = m_functions.waitForSingleObject(m_event.get(), m_waitTimeoutMilliseconds);

    if (waitResult == WAIT_OBJECT_0)
    {
        const std::uint64_t completedAfterEvent = completed_value();

        if (completedAfterEvent == k_deviceRemovedCompletedValue)
        {
            return resolve_abnormal_wait(
                make_error(*m_assertContext, k_fenceWaitIncomplete, "D3D12 Fence Event completed after device removal"),
                a_fenceValue, a_purpose);
        }

        if (completedAfterEvent >= a_fenceValue)
        {
            m_lastWaitCompletionProven = true;
            return Result<void>::success();
        }

        return resolve_abnormal_wait(
            make_error(*m_assertContext, k_fenceWaitIncomplete, "D3D12 Fence Event completed before the target value"),
            a_fenceValue, a_purpose);
    }

    if (waitResult == WAIT_TIMEOUT)
    {
        return resolve_abnormal_wait(make_error(*m_assertContext, k_fenceWaitTimeout, "D3D12 Fence Wait timed out"),
                                     a_fenceValue, a_purpose);
    }

    if (waitResult == WAIT_FAILED)
    {
        const DWORD nativeError = m_functions.getLastError();
        return resolve_abnormal_wait(make_native_error(*m_assertContext, k_fenceWaitPrimitiveFailed,
                                                       "D3D12 Fence Wait primitive failed", "Win32",
                                                       static_cast<std::int64_t>(nativeError)),
                                     a_fenceValue, a_purpose);
    }

    return resolve_abnormal_wait(make_native_error(*m_assertContext, k_fenceWaitPrimitiveFailed,
                                                   "D3D12 Fence Wait returned an unexpected result", "Win32",
                                                   static_cast<std::int64_t>(waitResult)),
                                 a_fenceValue, a_purpose);
}

Result<void> D3d12QueueState::resolve_abnormal_wait(Error &&a_waitError, std::uint64_t a_fenceValue,
                                                    D3d12FenceWaitPurpose a_purpose) noexcept
{
    HRESULT removalReason = m_functions.getDeviceRemovedReason(m_device);
    const std::uint64_t finalCompletedValue = completed_value();

    if (finalCompletedValue == k_deviceRemovedCompletedValue && SUCCEEDED(removalReason))
    {
        removalReason = m_functions.getDeviceRemovedReason(m_device);
    }

    if (FAILED(removalReason) || finalCompletedValue == k_deviceRemovedCompletedValue)
    {
        m_status = D3d12QueueStateStatus::DeviceRemoved;
        return Result<void>::failure(
            make_device_removed_error(*m_assertContext, removalReason, std::move(a_waitError)));
    }

    if (finalCompletedValue >= a_fenceValue)
    {
        m_lastWaitCompletionProven = true;

        if (a_purpose == D3d12FenceWaitPurpose::Reusable)
        {
            return replace_wait_event(std::move(a_waitError));
        }

        return Result<void>::failure(std::move(a_waitError));
    }

    a_waitError.add_context(m_assertContext->fatal_handler(),
                            "D3D12 Fence completion and Device Removal could not be proven");
    m_status = D3d12QueueStateStatus::Unavailable;
    return Result<void>::failure(std::move(a_waitError));
}

Result<void> D3d12QueueState::replace_wait_event(Error &&a_cause) noexcept
{
    if (!m_functions.closeHandle(m_event.get()))
    {
        const DWORD nativeError = m_functions.getLastError();
        NativeError recoveryNativeError =
            NativeError::create(m_assertContext->fatal_handler(), "Win32", static_cast<std::int64_t>(nativeError));
        m_status = D3d12QueueStateStatus::Unavailable;
        return Result<void>::failure(Error::reclassify(
            m_assertContext->fatal_handler(), make_code(*m_assertContext, k_fenceWaitEventRecoveryFailed),
            "D3D12 Fence Wait Event recovery failed", std::move(recoveryNativeError), std::move(a_cause)));
    }

    m_event.mark_closed();
    HANDLE replacement = m_functions.createEvent(nullptr, FALSE, FALSE, nullptr);

    if (replacement == nullptr)
    {
        const DWORD nativeError = m_functions.getLastError();
        NativeError recoveryNativeError =
            NativeError::create(m_assertContext->fatal_handler(), "Win32", static_cast<std::int64_t>(nativeError));
        m_status = D3d12QueueStateStatus::Unavailable;
        return Result<void>::failure(Error::reclassify(
            m_assertContext->fatal_handler(), make_code(*m_assertContext, k_fenceWaitEventRecoveryFailed),
            "D3D12 Fence Wait Event recovery failed", std::move(recoveryNativeError), std::move(a_cause)));
    }

    m_event.reset(replacement, m_functions.closeHandle);
    return Result<void>::failure(std::move(a_cause));
}

Result<void> D3d12QueueState::shutdown() noexcept
{
    if (m_status == D3d12QueueStateStatus::Shutdown)
    {
        return Result<void>::success();
    }

    CUE_ASSERT(*m_assertContext, m_status == D3d12QueueStateStatus::Ready,
               "D3D12 Queue shutdown requires a ready Queue");
    Result<std::uint64_t> reservationResult = reserve_fence_value();

    if (!reservationResult)
    {
        Error reservationError = std::move(*reservationResult.try_error());

        if (m_lastSignaledFence == 0)
        {
            Result<void> releaseResult = release_native_objects();
            retain_secondary_error(reservationError, releaseResult,
                                   "D3D12 Queue cleanup also failed after Fence exhaustion", *m_assertContext);
            return Result<void>::failure(std::move(reservationError));
        }

        Result<void> drainResult = wait_for_fence(m_lastSignaledFence, D3d12FenceWaitPurpose::BackendTerminal);

        if (!drainResult && !m_lastWaitCompletionProven)
        {
            if (m_status == D3d12QueueStateStatus::DeviceRemoved)
            {
                add_secondary_error_context(*drainResult.try_error(), reservationError,
                                            "D3D12 Fence range was also exhausted before Device Removal",
                                            *m_assertContext);
                return drainResult;
            }

            retain_secondary_error(reservationError, drainResult,
                                   "D3D12 Queue drain also failed after Fence exhaustion", *m_assertContext);
            return Result<void>::failure(std::move(reservationError));
        }

        retain_secondary_error(reservationError, drainResult,
                               "D3D12 Queue drain reported an Error after completion was proven", *m_assertContext);
        Result<void> releaseResult = release_native_objects();
        retain_secondary_error(reservationError, releaseResult,
                               "D3D12 Queue cleanup also failed after Fence exhaustion", *m_assertContext);
        return Result<void>::failure(std::move(reservationError));
    }

    const std::uint64_t terminalValue = *reservationResult.try_value();
    Result<void> signalResult = signal_reserved(terminalValue);

    if (!signalResult)
    {
        Result<void> resolutionResult = resolve_failed_signal(terminalValue, std::move(*signalResult.try_error()),
                                                              D3d12FenceWaitPurpose::BackendTerminal);

        if (m_lastWaitCompletionProven)
        {
            Error resolutionError = std::move(*resolutionResult.try_error());
            Result<void> releaseResult = release_native_objects();
            retain_secondary_error(resolutionError, releaseResult,
                                   "D3D12 Queue cleanup also failed after terminal Signal recovery", *m_assertContext);
            return Result<void>::failure(std::move(resolutionError));
        }

        return resolutionResult;
    }

    Result<void> waitResult = wait_for_fence(terminalValue, D3d12FenceWaitPurpose::BackendTerminal);

    if (!waitResult && !m_lastWaitCompletionProven)
    {
        return waitResult;
    }

    Result<void> releaseResult = release_native_objects();

    if (!waitResult)
    {
        Error waitError = std::move(*waitResult.try_error());
        retain_secondary_error(waitError, releaseResult, "D3D12 Queue cleanup also failed after terminal Wait recovery",
                               *m_assertContext);
        return Result<void>::failure(std::move(waitError));
    }

    return releaseResult;
}

Result<void> D3d12QueueState::release_after_device_removed() noexcept
{
    CUE_ASSERT(*m_assertContext, m_status == D3d12QueueStateStatus::DeviceRemoved,
               "Device Removed release requires DeviceRemoved Queue state");
    return release_native_objects();
}

Result<void> D3d12QueueState::release_native_objects() noexcept
{
    std::optional<Error> closeError;

    if (m_event.is_open())
    {
        if (!m_functions.closeHandle(m_event.get()))
        {
            const DWORD nativeError = m_functions.getLastError();
            closeError.emplace(make_native_error(*m_assertContext, k_fenceWaitEventRecoveryFailed,
                                                 "D3D12 Fence Event could not be closed", "Win32",
                                                 static_cast<std::int64_t>(nativeError)));
        }
        else
        {
            m_event.mark_closed();
        }
    }

    m_fence.Reset();
    m_queue.Reset();
    m_status = D3d12QueueStateStatus::Shutdown;

    if (closeError)
    {
        return Result<void>::failure(std::move(*closeError));
    }

    return Result<void>::success();
}

D3d12QueueStateStatus D3d12QueueState::status() const noexcept
{
    return m_status;
}

std::uint64_t D3d12QueueState::next_fence_value() const noexcept
{
    return m_nextFenceValue;
}

std::uint64_t D3d12QueueState::last_signaled_fence() const noexcept
{
    return m_lastSignaledFence;
}

bool D3d12QueueState::was_last_wait_completion_proven() const noexcept
{
    return m_lastWaitCompletionProven;
}

bool D3d12QueueState::has_native_objects() const noexcept
{
    return m_queue != nullptr || m_fence != nullptr || m_event.is_open();
}

bool D3d12QueueState::has_gpu_objects() const noexcept
{
    return m_queue != nullptr || m_fence != nullptr;
}

bool D3d12QueueState::has_queue() const noexcept
{
    return m_queue != nullptr;
}

bool D3d12QueueState::has_fence() const noexcept
{
    return m_fence != nullptr;
}

bool D3d12QueueState::has_fence_event() const noexcept
{
    return m_event.is_open();
}

ID3D12CommandQueue *D3d12QueueState::native_queue_for_presentation() const noexcept
{
    CUE_ASSERT(*m_assertContext, m_status == D3d12QueueStateStatus::Ready,
               "D3D12 Presentation requires a ready Direct Queue");
    return m_status == D3d12QueueStateStatus::Ready ? m_queue.Get() : nullptr;
}

void D3d12QueueState::set_next_fence_value_for_test(std::uint64_t a_nextFenceValue) noexcept
{
    CUE_ASSERT(*m_assertContext, a_nextFenceValue >= 1, "Test Fence value must be non-zero");
    m_nextFenceValue = a_nextFenceValue;
    m_lastAttemptedFence = a_nextFenceValue - 1;
}

Result<D3d12QueueState> create_d3d12_queue_state(ID3D12Device *a_device, std::uint32_t a_waitTimeoutMilliseconds,
                                                 const AssertContext &a_assertContext,
                                                 const D3d12QueueNativeFunctions &a_functions) noexcept
{
    D3D12_COMMAND_QUEUE_DESC descriptor = {};
    descriptor.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    descriptor.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    descriptor.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    descriptor.NodeMask = 0;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    const HRESULT queueResult = a_device->CreateCommandQueue(&descriptor, IID_PPV_ARGS(&queue));

    if (FAILED(queueResult))
    {
        return Result<D3d12QueueState>::failure(make_native_error(a_assertContext, k_queueCreationFailed,
                                                                  "D3D12 Direct Command Queue creation failed", "D3D12",
                                                                  static_cast<std::int64_t>(queueResult)));
    }

    const HRESULT queueNameResult = queue->SetName(L"CueEngine D3D12 Direct Graphics Queue");

    if (FAILED(queueNameResult))
    {
        return Result<D3d12QueueState>::failure(make_native_error(
            a_assertContext, k_queueNameFailed, "D3D12 Direct Command Queue diagnostic name could not be set", "D3D12",
            static_cast<std::int64_t>(queueNameResult)));
    }

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    const HRESULT fenceResult = a_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

    if (FAILED(fenceResult))
    {
        return Result<D3d12QueueState>::failure(make_native_error(a_assertContext, k_fenceCreationFailed,
                                                                  "D3D12 Queue Fence creation failed", "D3D12",
                                                                  static_cast<std::int64_t>(fenceResult)));
    }

    const HRESULT fenceNameResult = fence->SetName(L"CueEngine D3D12 Direct Graphics Queue Fence");

    if (FAILED(fenceNameResult))
    {
        return Result<D3d12QueueState>::failure(make_native_error(a_assertContext, k_fenceNameFailed,
                                                                  "D3D12 Queue Fence diagnostic name could not be set",
                                                                  "D3D12", static_cast<std::int64_t>(fenceNameResult)));
    }

    HANDLE eventHandle = a_functions.createEvent(nullptr, FALSE, FALSE, nullptr);

    if (eventHandle == nullptr)
    {
        const DWORD nativeError = a_functions.getLastError();
        return Result<D3d12QueueState>::failure(make_native_error(a_assertContext, k_eventCreationFailed,
                                                                  "D3D12 Fence Wait Event creation failed", "Win32",
                                                                  static_cast<std::int64_t>(nativeError)));
    }

    D3d12FenceEvent event(eventHandle, a_functions.closeHandle);
    D3d12QueueState state(std::move(queue), std::move(fence), std::move(event), a_device, a_waitTimeoutMilliseconds,
                          a_functions, a_assertContext);
    return Result<D3d12QueueState>::success(std::move(state));
}
} // namespace cue
