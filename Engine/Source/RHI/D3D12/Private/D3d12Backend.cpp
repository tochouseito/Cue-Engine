#include <Cue/RHI/D3D12/D3d12Backend.h>

#include "D3d12AdapterSelection.h"
#include "D3d12DeviceCreation.h"
#include "D3d12Diagnostics.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
constexpr std::int64_t k_deviceNameFailed = 31;
constexpr std::int64_t k_capabilityQueryFailed = 32;
constexpr std::int64_t k_backendUnavailable = 33;
constexpr std::int64_t k_deviceRemoved = 34;

[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("D3D12 Backend allocation failed");
    std::abort();
}

[[nodiscard]] cue::Error make_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                    std::string_view a_summary) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(
        a_context.fatal_handler(), "Cue.RHI.D3D12", a_code);
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary);
}

[[nodiscard]] cue::Error make_native_error(const cue::AssertContext &a_context, std::int64_t a_code,
                                           std::string_view a_summary, HRESULT a_nativeCode) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(
        a_context.fatal_handler(), "Cue.RHI.D3D12", a_code);
    cue::NativeError nativeError = cue::NativeError::create(
        a_context.fatal_handler(), "D3D12", static_cast<std::int64_t>(a_nativeCode));
    return cue::Error::create(
        a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
}

void add_error_identity_context(cue::Error &a_primaryError, std::string_view a_label,
                                const cue::ErrorCode &a_code,
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
        terminate_allocation(a_assertContext);
    }
}

void add_secondary_error_context(cue::Error &a_primaryError, const cue::Error &a_secondaryError,
                                 std::string_view a_context,
                                 const cue::AssertContext &a_assertContext) noexcept
{
    a_primaryError.add_context(a_assertContext.fatal_handler(), a_context);
    a_primaryError.add_context(a_assertContext.fatal_handler(), a_secondaryError.summary());
    add_error_identity_context(
        a_primaryError, "Secondary shutdown Error", a_secondaryError.code(),
        a_secondaryError.try_native_error(), a_assertContext);

    for (const cue::ErrorContext &context : a_secondaryError.contexts())
    {
        a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
    }

    for (const cue::ErrorCause &cause : a_secondaryError.causes())
    {
        a_primaryError.add_context(
            a_assertContext.fatal_handler(), "Secondary shutdown Error cause");
        a_primaryError.add_context(a_assertContext.fatal_handler(), cause.summary());
        add_error_identity_context(
            a_primaryError, "Secondary shutdown Error cause", cause.code(),
            cause.try_native_error(), a_assertContext);

        for (const cue::ErrorContext &context : cause.contexts())
        {
            a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
        }
    }
}

void retain_shutdown_error(std::optional<cue::Error> &a_firstError,
                           cue::Result<void> &a_result, std::string_view a_context,
                           const cue::AssertContext &a_assertContext) noexcept
{
    if (a_result)
    {
        return;
    }

    cue::Error *error = a_result.try_error();

    if (!a_firstError)
    {
        a_firstError.emplace(std::move(*error));
        return;
    }

    add_secondary_error_context(*a_firstError, *error, a_context, a_assertContext);
}

class D3d12BackendImpl final : public cue::D3d12Backend
{
  public:
    D3d12BackendImpl(cue::D3d12AdapterSelection &&a_selection,
                     Microsoft::WRL::ComPtr<ID3D12Device> a_device,
                     cue::D3d12DiagnosticsStatus a_diagnostics,
                     cue::CapabilityReport &&a_capabilities,
                     cue::AssertContext &a_assertContext) noexcept
        : m_factory(std::move(a_selection.factory)), m_adapter(std::move(a_selection.adapter)),
          m_device(std::move(a_device)), m_capabilities(std::move(a_capabilities)),
          m_diagnostics(a_diagnostics), m_assertContext(&a_assertContext),
          m_creationThread(std::this_thread::get_id()), m_state(cue::GraphicsBackendState::Ready)
    {
    }

    ~D3d12BackendImpl() noexcept override
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Backend must be destroyed on the creation thread");

        if (m_state != cue::GraphicsBackendState::Shutdown || m_device != nullptr ||
            m_adapter != nullptr || m_factory != nullptr)
        {
            m_assertContext->fatal_handler().terminate(
                "D3D12 Backend owner was destroyed before shutdown");
        }
    }

    [[nodiscard]] const cue::CapabilityReport &capabilities() const noexcept override
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Backend capabilities must be queried on the creation thread");
        return m_capabilities;
    }

    [[nodiscard]] cue::GraphicsBackendState state() const noexcept override
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Backend state must be queried on the creation thread");
        return m_state;
    }

    [[nodiscard]] cue::Result<void> shutdown() noexcept override
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Backend shutdown must run on the creation thread");

        if (m_state == cue::GraphicsBackendState::Shutdown)
        {
            return cue::Result<void>::success();
        }

        if (m_state == cue::GraphicsBackendState::Unavailable)
        {
            return cue::Result<void>::failure(make_error(
                *m_assertContext, k_backendUnavailable, "D3D12 Backend is unavailable"));
        }

        std::optional<cue::Error> firstError;
        HRESULT removalReason = m_device->GetDeviceRemovedReason();

        if (FAILED(removalReason))
        {
            m_state = cue::GraphicsBackendState::DeviceRemoved;
            firstError.emplace(make_native_error(
                *m_assertContext, k_deviceRemoved, "D3D12 Device was removed", removalReason));

            cue::Result<void> dredResult = cue::collect_d3d12_device_removed_diagnostics(
                m_device.Get(), m_diagnostics, *m_assertContext);
            retain_shutdown_error(
                firstError, dredResult,
                "D3D12 DRED diagnostics also failed while handling device removal",
                *m_assertContext);
        }

        cue::Result<void> liveObjectResult = cue::report_d3d12_live_device_objects(
            m_device.Get(), m_diagnostics, *m_assertContext);

        retain_shutdown_error(
            firstError, liveObjectResult, "D3D12 Live Object diagnostics also failed",
            *m_assertContext);

        cue::Result<void> messageResult = cue::log_d3d12_messages_at_quiescent_point(
            m_device.Get(), m_diagnostics, "D3D12 Backend shutdown", *m_assertContext);

        retain_shutdown_error(
            firstError, messageResult, "D3D12 InfoQueue diagnostics also failed",
            *m_assertContext);

        m_device.Reset();
        m_adapter.Reset();
        m_factory.Reset();
        m_state = cue::GraphicsBackendState::Shutdown;

        if (firstError)
        {
            return cue::Result<void>::failure(std::move(*firstError));
        }

        return cue::Result<void>::success();
    }

  private:
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    cue::CapabilityReport m_capabilities;
    cue::D3d12DiagnosticsStatus m_diagnostics;
    cue::AssertContext *m_assertContext;
    std::thread::id m_creationThread;
    cue::GraphicsBackendState m_state;
};
} // namespace

namespace cue
{
D3d12Backend::~D3d12Backend() noexcept = default;

Result<std::unique_ptr<D3d12Backend>> create_d3d12_backend(
    const D3d12BackendDescriptor &a_descriptor, AssertContext &a_assertContext) noexcept
{
    Result<D3d12DiagnosticsStatus> diagnosticsResult =
        configure_d3d12_pre_device_diagnostics(a_descriptor, a_assertContext);

    if (!diagnosticsResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(
            std::move(*diagnosticsResult.try_error()));
    }

    D3d12DiagnosticsStatus diagnostics = *diagnosticsResult.try_value();
    Result<D3d12AdapterSelection> selectionResult =
        select_d3d12_adapter(a_descriptor.adapterPolicy, diagnostics, a_assertContext);

    if (!selectionResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(
            std::move(*selectionResult.try_error()));
    }

    D3d12AdapterSelection selection = std::move(*selectionResult.try_value());
    Result<Microsoft::WRL::ComPtr<ID3D12Device>> deviceResult = create_d3d12_device(
        selection.adapter.Get(), selection.featureLevel, a_assertContext);

    if (!deviceResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(std::move(*deviceResult.try_error()));
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device = std::move(*deviceResult.try_value());
    HRESULT nameResult = device->SetName(L"CueEngine D3D12 Device");

    if (FAILED(nameResult))
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(make_native_error(
            a_assertContext, k_deviceNameFailed, "D3D12 Device diagnostic name could not be set",
            nameResult));
    }

    Result<void> infoQueueResult = configure_d3d12_info_queue(
        device.Get(), diagnostics, a_assertContext);

    if (!infoQueueResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(
            std::move(*infoQueueResult.try_error()));
    }

    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture = {};
    architecture.NodeIndex = 0;
    HRESULT capabilityResult = device->CheckFeatureSupport(
        D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture));

    if (FAILED(capabilityResult))
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(make_native_error(
            a_assertContext, k_capabilityQueryFailed,
            "D3D12 Device architecture capability could not be queried", capabilityResult));
    }

    Result<CapabilityReport> capabilityReportResult = make_d3d12_capability_report(
        selection.report, architecture.UMA != FALSE, a_assertContext);

    if (!capabilityReportResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(
            std::move(*capabilityReportResult.try_error()));
    }

    try
    {
        std::unique_ptr<D3d12Backend> backend = std::make_unique<D3d12BackendImpl>(
            std::move(selection), std::move(device), diagnostics,
            std::move(*capabilityReportResult.try_value()), a_assertContext);
        return Result<std::unique_ptr<D3d12Backend>>::success(std::move(backend));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}
} // namespace cue
