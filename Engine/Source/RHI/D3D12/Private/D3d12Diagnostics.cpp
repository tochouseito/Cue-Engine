#include "D3d12Diagnostics.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Log.h>

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#ifndef CUE_D3D12_DIAGNOSTICS_ALLOWED
#error CUE_D3D12_DIAGNOSTICS_ALLOWED must be provided by the Cue.RHI.D3D12 CMake target
#endif

namespace
{
constexpr std::int64_t k_invalidConfiguration = 1;
constexpr std::int64_t k_invalidDevice = 2;
constexpr std::int64_t k_infoQueueRollbackFailed = 3;
constexpr std::int64_t k_infoQueueMessageFailed = 4;
constexpr std::int64_t k_deviceRemovedDiagnosticsFailed = 5;
constexpr std::int64_t k_optionalDiagnosticsUnavailable = 6;
constexpr std::int64_t k_diagnosticMessagesDropped = 7;
constexpr std::uint64_t k_maxDiagnosticMessages = 4096;

[[nodiscard]] cue::Error make_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                    std::string_view a_summary) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.RHI.D3D12", a_code);
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary);
}

[[nodiscard]] cue::Error make_native_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                           std::string_view a_summary, HRESULT a_nativeCode) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_context.fatal_handler(), "Cue.RHI.D3D12", a_code);
    cue::NativeError nativeError = cue::NativeError::create(
        a_context.fatal_handler(), "D3D12", static_cast<std::int64_t>(a_nativeCode));
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
}

void log_fallback(const cue::AssertContext &a_context, std::string_view a_message, HRESULT a_nativeCode) noexcept
{
    cue::Error error = make_native_error(a_context, k_optionalDiagnosticsUnavailable,
                                         "Optional D3D12 diagnostics are unavailable", a_nativeCode);
    [[maybe_unused]] cue::LogResult logResult =
        a_context.logger().log(cue::LogLevel::Warning, a_message, std::move(error));
}

[[nodiscard]] cue::LogLevel to_log_level(D3D12_MESSAGE_SEVERITY a_severity) noexcept
{
    switch (a_severity)
    {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    case D3D12_MESSAGE_SEVERITY_ERROR:
        return cue::LogLevel::Error;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        return cue::LogLevel::Warning;
    case D3D12_MESSAGE_SEVERITY_INFO:
        return cue::LogLevel::Info;
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
    default:
        return cue::LogLevel::Debug;
    }
}

[[nodiscard]] HRESULT rollback_info_queue_breaks(ID3D12InfoQueue &a_infoQueue) noexcept
{
    HRESULT corruptionResult = a_infoQueue.SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
    HRESULT errorResult = a_infoQueue.SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
    return FAILED(corruptionResult) ? corruptionResult : errorResult;
}

[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("D3D12 diagnostics allocation failed");
    std::abort();
}
} // namespace

namespace cue
{
bool are_d3d12_diagnostics_allowed() noexcept
{
    return CUE_D3D12_DIAGNOSTICS_ALLOWED != 0;
}

Result<D3d12DiagnosticsStatus> configure_d3d12_pre_device_diagnostics(
    const D3d12BackendDescriptor &a_descriptor, const AssertContext &a_assertContext) noexcept
{
    D3d12DiagnosticsStatus status = {};

    if (!are_d3d12_diagnostics_allowed())
    {
        if (a_descriptor.validationMode != D3d12ValidationMode::Disabled || a_descriptor.isDredEnabled)
        {
            return Result<D3d12DiagnosticsStatus>::failure(make_error(
                a_assertContext, k_invalidConfiguration, "D3D12 diagnostics are forbidden in Release builds"));
        }

        return Result<D3d12DiagnosticsStatus>::success(std::move(status));
    }

    if (a_descriptor.isDredEnabled)
    {
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
        HRESULT dredResult = D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings));

        if (SUCCEEDED(dredResult))
        {
            dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_FORCED_OFF);
            status.isDredEnabled = true;
        }
        else
        {
            log_fallback(a_assertContext, "D3D12 DREDを利用できないため診断なしで続行します", dredResult);
        }
    }

    if (a_descriptor.validationMode == D3d12ValidationMode::Disabled)
    {
        return Result<D3d12DiagnosticsStatus>::success(std::move(status));
    }

    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    HRESULT debugResult = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));

    if (FAILED(debugResult))
    {
        log_fallback(a_assertContext, "D3D12 Debug Layerを利用できないため診断なしで続行します", debugResult);
        return Result<D3d12DiagnosticsStatus>::success(std::move(status));
    }

    debugController->EnableDebugLayer();
    status.isDebugLayerEnabled = true;

    if (a_descriptor.validationMode == D3d12ValidationMode::GpuBased)
    {
        Microsoft::WRL::ComPtr<ID3D12Debug1> gpuValidationController;
        HRESULT gpuValidationResult = debugController.As(&gpuValidationController);

        if (FAILED(gpuValidationResult))
        {
            log_fallback(a_assertContext, "D3D12 GPU Based Validationを利用できないためStandardで続行します",
                         gpuValidationResult);
        }
        else
        {
            gpuValidationController->SetEnableGPUBasedValidation(TRUE);
        }
    }

    return Result<D3d12DiagnosticsStatus>::success(std::move(status));
}

Result<void> configure_d3d12_info_queue(ID3D12Device *a_device, D3d12DiagnosticsStatus &a_status,
                                        const AssertContext &a_assertContext) noexcept
{
    a_status.isInfoQueueEnabled = false;

    if (!a_status.isDebugLayerEnabled)
    {
        return Result<void>::success();
    }

    if (a_device == nullptr)
    {
        return Result<void>::failure(
            make_error(a_assertContext, k_invalidDevice, "D3D12 Device is required for InfoQueue configuration"));
    }

    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    HRESULT queryResult = a_device->QueryInterface(IID_PPV_ARGS(&infoQueue));

    if (FAILED(queryResult))
    {
        log_fallback(a_assertContext, "D3D12 InfoQueueを利用できないため診断なしで続行します", queryResult);
        return Result<void>::success();
    }

    infoQueue->ClearStorageFilter();
    infoQueue->ClearRetrievalFilter();

    for (D3D12_MESSAGE_SEVERITY severity : {D3D12_MESSAGE_SEVERITY_CORRUPTION, D3D12_MESSAGE_SEVERITY_ERROR})
    {
        HRESULT breakResult = infoQueue->SetBreakOnSeverity(severity, TRUE);

        if (FAILED(breakResult))
        {
            HRESULT rollbackResult = rollback_info_queue_breaks(*infoQueue.Get());

            if (FAILED(rollbackResult))
            {
                return Result<void>::failure(make_native_error(
                    a_assertContext, k_infoQueueRollbackFailed,
                    "D3D12 InfoQueue Break policy could not be rolled back", rollbackResult));
            }

            log_fallback(a_assertContext, "D3D12 InfoQueueのBreak Policyを設定できないため診断なしで続行します",
                         breakResult);
            return Result<void>::success();
        }
    }

    HRESULT warningBreakResult = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

    if (FAILED(warningBreakResult))
    {
        HRESULT rollbackResult = rollback_info_queue_breaks(*infoQueue.Get());

        if (FAILED(rollbackResult))
        {
            return Result<void>::failure(make_native_error(
                a_assertContext, k_infoQueueRollbackFailed,
                "D3D12 InfoQueue Break policy could not be rolled back", rollbackResult));
        }

        log_fallback(a_assertContext, "D3D12 InfoQueueのWarning Policyを設定できないため診断なしで続行します",
                     warningBreakResult);
        return Result<void>::success();
    }

    a_status.isInfoQueueEnabled = true;
    return Result<void>::success();
}

Result<void> log_d3d12_messages(ID3D12Device *a_device, const D3d12DiagnosticsStatus &a_status,
                                std::string_view a_context, const AssertContext &a_assertContext) noexcept
{
    if (!a_status.isInfoQueueEnabled)
    {
        return Result<void>::success();
    }

    if (a_device == nullptr)
    {
        return Result<void>::failure(
            make_error(a_assertContext, k_invalidDevice, "D3D12 Device is required for InfoQueue logging"));
    }

    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    HRESULT queryResult = a_device->QueryInterface(IID_PPV_ARGS(&infoQueue));

    if (FAILED(queryResult))
    {
        return Result<void>::failure(make_native_error(a_assertContext, k_infoQueueMessageFailed,
                                                       "D3D12 InfoQueue is unavailable", queryResult));
    }

    const std::uint64_t messageCount =
        (std::min)(infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter(), k_maxDiagnosticMessages);

    for (std::uint64_t index = 0; index < messageCount; ++index)
    {
        SIZE_T messageSize = 0;
        HRESULT sizeResult = infoQueue->GetMessage(index, nullptr, &messageSize);

        if (FAILED(sizeResult))
        {
            infoQueue->ClearStoredMessages();
            return Result<void>::failure(make_native_error(a_assertContext, k_infoQueueMessageFailed,
                                                           "D3D12 InfoQueue message size could not be read",
                                                           sizeResult));
        }

        try
        {
            std::vector<std::byte> messageStorage(messageSize);
            D3D12_MESSAGE *message = reinterpret_cast<D3D12_MESSAGE *>(messageStorage.data());
            HRESULT messageResult = infoQueue->GetMessage(index, message, &messageSize);

            if (FAILED(messageResult))
            {
                infoQueue->ClearStoredMessages();
                return Result<void>::failure(make_native_error(a_assertContext, k_infoQueueMessageFailed,
                                                               "D3D12 InfoQueue message could not be read",
                                                               messageResult));
            }

            std::string_view description = message->pDescription != nullptr
                                               ? std::string_view(message->pDescription)
                                               : std::string_view("D3D12 message description is unavailable");
            ErrorCode code = ErrorCode::create(a_assertContext.fatal_handler(), "D3D12.Message",
                                               static_cast<std::int64_t>(message->ID));
            Error error = Error::create(a_assertContext.fatal_handler(), std::move(code), description);
            error.add_context(a_assertContext.fatal_handler(), a_context);
            [[maybe_unused]] LogResult logResult = a_assertContext.logger().log(
                to_log_level(message->Severity), "D3D12診断メッセージを取得しました", std::move(error));
        }
        catch (...)
        {
            terminate_allocation(a_assertContext);
        }
    }

    const std::uint64_t storedMessageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();

    if (storedMessageCount > messageCount)
    {
        ErrorCode code = ErrorCode::create(a_assertContext.fatal_handler(), "D3D12.Message",
                                           k_diagnosticMessagesDropped);
        NativeError droppedCount = NativeError::create(
            a_assertContext.fatal_handler(), "D3D12.DroppedMessageCount",
            static_cast<std::int64_t>(storedMessageCount - messageCount));
        Error error = Error::create(a_assertContext.fatal_handler(), std::move(code),
                                    "D3D12 diagnostic message limit was exceeded", std::move(droppedCount));
        error.add_context(a_assertContext.fatal_handler(), a_context);
        [[maybe_unused]] LogResult logResult = a_assertContext.logger().log(
            LogLevel::Warning, "D3D12診断メッセージの上限超過分を破棄します", std::move(error));
    }

    infoQueue->ClearStoredMessages();
    return Result<void>::success();
}

Result<void> collect_d3d12_device_removed_diagnostics(ID3D12Device *a_device,
                                                       const D3d12DiagnosticsStatus &a_status,
                                                       const AssertContext &a_assertContext) noexcept
{
    if (a_device == nullptr)
    {
        return Result<void>::failure(
            make_error(a_assertContext, k_invalidDevice, "D3D12 Device is required for removal diagnostics"));
    }

    if (!a_status.isDredEnabled || !are_d3d12_diagnostics_allowed())
    {
        return Result<void>::success();
    }

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    HRESULT queryResult = a_device->QueryInterface(IID_PPV_ARGS(&dred));

    if (FAILED(queryResult))
    {
        log_fallback(a_assertContext, "D3D12 Device RemovalのDRED Interfaceを取得できません", queryResult);
        return Result<void>::success();
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
    HRESULT breadcrumbsResult = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);

    if (FAILED(breadcrumbsResult))
    {
        log_fallback(a_assertContext, "D3D12 Device RemovalのAuto Breadcrumbを取得できません",
                     breadcrumbsResult);
    }
    else
    {
        [[maybe_unused]] LogResult breadcrumbsLogResult = a_assertContext.logger().log(
            LogLevel::Error, breadcrumbs.pHeadAutoBreadcrumbNode == nullptr
                                 ? "D3D12 Device RemovalのAuto Breadcrumbは空です"
                                 : "D3D12 Device RemovalのAuto Breadcrumbを取得しました");
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
    HRESULT pageFaultResult = dred->GetPageFaultAllocationOutput1(&pageFault);

    if (FAILED(pageFaultResult))
    {
        log_fallback(a_assertContext, "D3D12 Device RemovalのPage Fault情報を取得できません", pageFaultResult);
    }
    else
    {
        NativeError pageFaultAddress = NativeError::create(
            a_assertContext.fatal_handler(), "D3D12.GpuVirtualAddress",
            static_cast<std::int64_t>(pageFault.PageFaultVA));
        ErrorCode code = ErrorCode::create(a_assertContext.fatal_handler(), "Cue.RHI.D3D12",
                                           k_deviceRemovedDiagnosticsFailed);
        Error pageFaultError = Error::create(a_assertContext.fatal_handler(), std::move(code),
                                             "D3D12 Device Removal page fault data", std::move(pageFaultAddress));
        [[maybe_unused]] LogResult pageFaultLogResult = a_assertContext.logger().log(
            LogLevel::Error, "D3D12 Device RemovalのPage Fault情報を取得しました", std::move(pageFaultError));
    }

    return Result<void>::success();
}
} // namespace cue
