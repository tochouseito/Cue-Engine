#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12SwapChainProbe.h>

#include "D3d12AdapterSelection.h"
#include "D3d12DeviceCreation.h"
#include "D3d12Diagnostics.h"
#include "D3d12QueueState.h"
#include "D3d12RtvHeap.h"
#include "D3d12SwapChainState.h"

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
constexpr std::int64_t k_invalidWaitTimeout = 35;
constexpr std::int64_t k_activePresentationContexts = 87;
constexpr std::int64_t k_presentationUnavailable = 88;
constexpr std::int64_t k_deviceRemovalProbeUnavailable = 89;
constexpr std::uint32_t k_maximumWaitTimeoutMilliseconds = 60000;

[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("D3D12 Backend allocation failed");
    std::abort();
}

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
    cue::NativeError nativeError =
        cue::NativeError::create(a_context.fatal_handler(), "D3D12", static_cast<std::int64_t>(a_nativeCode));
    return cue::Error::create(a_context.fatal_handler(), std::move(code), a_summary, std::move(nativeError));
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
        terminate_allocation(a_assertContext);
    }
}

void add_secondary_error_context(cue::Error &a_primaryError, const cue::Error &a_secondaryError,
                                 std::string_view a_context, const cue::AssertContext &a_assertContext) noexcept
{
    a_primaryError.add_context(a_assertContext.fatal_handler(), a_context);
    a_primaryError.add_context(a_assertContext.fatal_handler(), a_secondaryError.summary());
    add_error_identity_context(a_primaryError, "Secondary shutdown Error", a_secondaryError.code(),
                               a_secondaryError.try_native_error(), a_assertContext);

    for (const cue::ErrorContext &context : a_secondaryError.contexts())
    {
        a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
    }

    for (const cue::ErrorCause &cause : a_secondaryError.causes())
    {
        a_primaryError.add_context(a_assertContext.fatal_handler(), "Secondary shutdown Error cause");
        a_primaryError.add_context(a_assertContext.fatal_handler(), cause.summary());
        add_error_identity_context(a_primaryError, "Secondary shutdown Error cause", cause.code(),
                                   cause.try_native_error(), a_assertContext);

        for (const cue::ErrorContext &context : cause.contexts())
        {
            a_primaryError.add_context(a_assertContext.fatal_handler(), context.message());
        }
    }
}

void retain_shutdown_error(std::optional<cue::Error> &a_firstError, cue::Result<void> &a_result,
                           std::string_view a_context, const cue::AssertContext &a_assertContext) noexcept
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

using UnregisterPresentation = void (*)(void *) noexcept;
using PrepareDeviceRemovedCleanup = cue::Result<void> (*)(void *) noexcept;

class D3d12PresentationContext final : public cue::PresentationContext
{
  public:
    D3d12PresentationContext(cue::D3d12SwapChainState &&a_swapChain, cue::D3d12RtvHeap &&a_rtvHeap,
                             cue::GraphicsBackend &a_backend, void *a_backendOwner,
                             UnregisterPresentation a_unregisterPresentation,
                             PrepareDeviceRemovedCleanup a_prepareDeviceRemovedCleanup,
                             cue::AssertContext &a_assertContext) noexcept
        : m_swapChain(std::move(a_swapChain)), m_rtvHeap(std::move(a_rtvHeap)), m_backend(&a_backend),
          m_backendOwner(a_backendOwner), m_unregisterPresentation(a_unregisterPresentation),
          m_prepareDeviceRemovedCleanup(a_prepareDeviceRemovedCleanup), m_assertContext(&a_assertContext),
          m_creationThread(std::this_thread::get_id()), m_state(cue::PresentationContextState::Ready),
          m_isRegistered(true)
    {
    }

    ~D3d12PresentationContext() noexcept override
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Presentation Context must be destroyed on the creation thread");

        if (m_state != cue::PresentationContextState::Shutdown || m_isRegistered || m_swapChain.has_native_objects() ||
            m_rtvHeap.has_native_object())
        {
            m_assertContext->fatal_handler().terminate(
                "D3D12 Presentation Context owner was destroyed before shutdown");
        }
    }

    [[nodiscard]] cue::PresentationContextState state() const noexcept override
    {
        assert_thread("D3D12 Presentation state must be queried on the creation thread");
        return m_state;
    }

    [[nodiscard]] std::uint32_t width() const noexcept override
    {
        assert_thread("D3D12 Presentation width must be queried on the creation thread");
        return m_swapChain.width();
    }

    [[nodiscard]] std::uint32_t height() const noexcept override
    {
        assert_thread("D3D12 Presentation height must be queried on the creation thread");
        return m_swapChain.height();
    }

    [[nodiscard]] std::uint32_t buffer_count() const noexcept override
    {
        assert_thread("D3D12 Presentation buffer count must be queried on the creation thread");
        return m_swapChain.buffer_count();
    }

    [[nodiscard]] std::uint32_t current_back_buffer_index() const noexcept override
    {
        assert_thread("D3D12 Presentation index must be queried on the creation thread");
        return m_swapChain.current_back_buffer_index();
    }

    [[nodiscard]] bool is_vsync_enabled() const noexcept override
    {
        assert_thread("D3D12 Presentation VSync state must be queried on the creation thread");
        return m_swapChain.is_vsync_enabled();
    }

    [[nodiscard]] bool is_tearing_supported() const noexcept override
    {
        assert_thread("D3D12 Presentation tearing capability must be queried on the creation thread");
        return m_swapChain.is_tearing_supported();
    }

    [[nodiscard]] bool is_tearing_enabled() const noexcept override
    {
        assert_thread("D3D12 Presentation tearing state must be queried on the creation thread");
        return m_swapChain.is_tearing_enabled();
    }

    [[nodiscard]] cue::Result<void> shutdown() noexcept override
    {
        assert_thread("D3D12 Presentation shutdown must run on the creation thread");

        if (m_state == cue::PresentationContextState::Shutdown)
        {
            return cue::Result<void>::success();
        }

        if (m_state == cue::PresentationContextState::Unavailable ||
            m_backend->state() == cue::GraphicsBackendState::Unavailable)
        {
            m_state = cue::PresentationContextState::Unavailable;
            return cue::Result<void>::failure(
                make_error(*m_assertContext, k_presentationUnavailable, "D3D12 Presentation Context is unavailable"));
        }

        if (m_backend->state() == cue::GraphicsBackendState::DeviceRemoved)
        {
            m_state = cue::PresentationContextState::DeviceRemoved;
        }

        std::optional<cue::Error> firstError;

        if (m_state == cue::PresentationContextState::DeviceRemoved)
        {
            cue::Result<void> diagnosticsResult = m_prepareDeviceRemovedCleanup(m_backendOwner);
            retain_shutdown_error(firstError, diagnosticsResult,
                                  "D3D12 Device Removal diagnostics failed before Presentation cleanup",
                                  *m_assertContext);
        }

        cue::Result<void> swapChainResult = m_swapChain.shutdown();
        cue::Result<void> rtvHeapResult = m_rtvHeap.shutdown();
        retain_shutdown_error(firstError, swapChainResult, "D3D12 Swap Chain cleanup also failed", *m_assertContext);
        retain_shutdown_error(firstError, rtvHeapResult, "D3D12 RTV Heap cleanup also failed", *m_assertContext);

        if (!swapChainResult || !rtvHeapResult)
        {
            m_state = cue::PresentationContextState::Unavailable;
            return cue::Result<void>::failure(std::move(*firstError));
        }

        unregister_from_backend();
        m_state = cue::PresentationContextState::Shutdown;

        if (firstError)
        {
            return cue::Result<void>::failure(std::move(*firstError));
        }

        return cue::Result<void>::success();
    }

  private:
    void assert_thread(std::string_view a_message) const noexcept
    {
        static_cast<void>(a_message);
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread, a_message);
    }

    void unregister_from_backend() noexcept
    {
        CUE_ASSERT(*m_assertContext, m_isRegistered, "D3D12 Presentation registration was already released");
        m_unregisterPresentation(m_backendOwner);
        m_isRegistered = false;
    }

    cue::D3d12SwapChainState m_swapChain;
    cue::D3d12RtvHeap m_rtvHeap;
    cue::GraphicsBackend *m_backend;
    void *m_backendOwner;
    UnregisterPresentation m_unregisterPresentation;
    PrepareDeviceRemovedCleanup m_prepareDeviceRemovedCleanup;
    cue::AssertContext *m_assertContext;
    std::thread::id m_creationThread;
    cue::PresentationContextState m_state;
    bool m_isRegistered;
};

class D3d12BackendImpl final : public cue::D3d12Backend
{
  public:
    D3d12BackendImpl(cue::D3d12AdapterSelection &&a_selection, Microsoft::WRL::ComPtr<ID3D12Device> a_device,
                     cue::D3d12QueueState &&a_queueState, cue::D3d12DiagnosticsStatus a_diagnostics,
                     cue::CapabilityReport &&a_capabilities, cue::AssertContext &a_assertContext) noexcept
        : m_factory(std::move(a_selection.factory)), m_adapter(std::move(a_selection.adapter)),
          m_device(std::move(a_device)), m_queueState(std::move(a_queueState)),
          m_capabilities(std::move(a_capabilities)), m_diagnostics(a_diagnostics), m_assertContext(&a_assertContext),
          m_creationThread(std::this_thread::get_id()), m_state(cue::GraphicsBackendState::Ready),
          m_activePresentationCount(0), m_dredCollectionAttemptCount(0)
    {
    }

    ~D3d12BackendImpl() noexcept override
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Backend must be destroyed on the creation thread");

        if (m_state != cue::GraphicsBackendState::Shutdown || m_activePresentationCount != 0 ||
            m_queueState.has_gpu_objects() || m_device != nullptr || m_adapter != nullptr || m_factory != nullptr)
        {
            m_assertContext->fatal_handler().terminate("D3D12 Backend owner was destroyed before shutdown");
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

    [[nodiscard]] cue::Result<void> force_device_removal_for_probe() noexcept
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Device Removal probe must run on the Backend creation thread");
        Microsoft::WRL::ComPtr<ID3D12Device5> device5;
        const HRESULT interfaceResult = m_device.As(&device5);

        if (FAILED(interfaceResult))
        {
            return cue::Result<void>::failure(make_native_error(*m_assertContext, k_deviceRemovalProbeUnavailable,
                                                                "ID3D12Device5 is unavailable for removal probe",
                                                                interfaceResult));
        }

        device5->RemoveDevice();
        cue::Error removalError = make_native_error(
            *m_assertContext, k_deviceRemoved, "D3D12 Device Removal probe was requested", DXGI_ERROR_DEVICE_REMOVED);
        return classify_presentation_native_failure(std::move(removalError));
    }

    [[nodiscard]] std::uint32_t dred_attempt_count_for_probe() const noexcept
    {
        return m_dredCollectionAttemptCount;
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
            return cue::Result<void>::failure(
                make_error(*m_assertContext, k_backendUnavailable, "D3D12 Backend is unavailable"));
        }

        std::optional<cue::Error> firstError;
        const bool isDeviceRemoved = m_state == cue::GraphicsBackendState::DeviceRemoved ||
                                     m_queueState.status() == cue::D3d12QueueStateStatus::DeviceRemoved;

        if (isDeviceRemoved)
        {
            m_state = cue::GraphicsBackendState::DeviceRemoved;
            cue::Result<void> dredResult = ensure_device_removed_diagnostics();
            retain_shutdown_error(firstError, dredResult, "D3D12 DRED diagnostics failed before Presentation cleanup",
                                  *m_assertContext);
        }

        if (m_activePresentationCount != 0)
        {
            cue::Error activeError = make_error(*m_assertContext, k_activePresentationContexts,
                                                "D3D12 Backend has active Presentation Contexts");

            if (firstError)
            {
                add_secondary_error_context(activeError, *firstError,
                                            "D3D12 Device Removal diagnostics also failed before active Context gate",
                                            *m_assertContext);
            }

            return cue::Result<void>::failure(std::move(activeError));
        }

        if (m_queueState.status() == cue::D3d12QueueStateStatus::DeviceRemoved)
        {
            cue::Result<void> queueReleaseResult = m_queueState.release_after_device_removed();
            retain_shutdown_error(firstError, queueReleaseResult,
                                  "D3D12 Queue cleanup also failed after device removal", *m_assertContext);

            if (m_queueState.status() == cue::D3d12QueueStateStatus::Unavailable)
            {
                m_state = cue::GraphicsBackendState::Unavailable;
                return cue::Result<void>::failure(std::move(*firstError));
            }
        }
        else
        {
            cue::Result<void> queueShutdownResult = m_queueState.shutdown();

            if (!queueShutdownResult)
            {
                firstError.emplace(std::move(*queueShutdownResult.try_error()));

                if (m_queueState.status() == cue::D3d12QueueStateStatus::Unavailable)
                {
                    m_state = cue::GraphicsBackendState::Unavailable;
                    return cue::Result<void>::failure(std::move(*firstError));
                }
            }

            if (m_queueState.status() == cue::D3d12QueueStateStatus::DeviceRemoved)
            {
                m_state = cue::GraphicsBackendState::DeviceRemoved;

                cue::Result<void> dredResult = ensure_device_removed_diagnostics();
                retain_shutdown_error(firstError, dredResult,
                                      "D3D12 DRED diagnostics also failed while handling device removal",
                                      *m_assertContext);

                cue::Result<void> queueReleaseResult = m_queueState.release_after_device_removed();
                retain_shutdown_error(firstError, queueReleaseResult,
                                      "D3D12 Queue cleanup also failed after device removal", *m_assertContext);

                if (m_queueState.status() == cue::D3d12QueueStateStatus::Unavailable)
                {
                    m_state = cue::GraphicsBackendState::Unavailable;
                    return cue::Result<void>::failure(std::move(*firstError));
                }
            }
        }

        cue::Result<void> liveObjectResult =
            cue::report_d3d12_live_device_objects(m_device.Get(), m_diagnostics, *m_assertContext);

        retain_shutdown_error(firstError, liveObjectResult, "D3D12 Live Object diagnostics also failed",
                              *m_assertContext);

        cue::Result<void> messageResult = cue::log_d3d12_messages_at_quiescent_point(
            m_device.Get(), m_diagnostics, "D3D12 Backend shutdown", *m_assertContext);

        retain_shutdown_error(firstError, messageResult, "D3D12 InfoQueue diagnostics also failed", *m_assertContext);

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
    [[nodiscard]] cue::Result<void> ensure_device_removed_diagnostics() noexcept
    {
        if (m_dredCollectionAttemptCount != 0)
        {
            return cue::Result<void>::success();
        }

        ++m_dredCollectionAttemptCount;
        return cue::collect_d3d12_device_removed_diagnostics(m_device.Get(), m_diagnostics, *m_assertContext);
    }

    [[nodiscard]] const cue::AssertContext &assert_context_for_presentation() const noexcept override
    {
        return *m_assertContext;
    }

    [[nodiscard]] cue::Result<std::unique_ptr<cue::PresentationContext>> create_windows_presentation(
        const void *a_nativeWindow, std::uint32_t a_width, std::uint32_t a_height,
        const cue::PresentationDescriptor &a_descriptor) noexcept override
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Presentation must be created on the Backend creation thread");

        if (m_state != cue::GraphicsBackendState::Ready)
        {
            return cue::Result<std::unique_ptr<cue::PresentationContext>>::failure(make_error(
                *m_assertContext, k_backendUnavailable, "D3D12 Backend cannot create a Presentation Context"));
        }

        cue::D3d12SwapChainDescriptor swapChainDescriptor = {
            static_cast<HWND>(const_cast<void *>(a_nativeWindow)),
            a_width,
            a_height,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            a_descriptor.isVsyncEnabled,
        };
        cue::D3d12SwapChainFailureHandler failureHandler = {
            this,
            handle_swap_chain_native_failure,
        };
        cue::Result<cue::D3d12SwapChainState> swapChainResult = cue::create_d3d12_swap_chain_state(
            m_factory.Get(), m_queueState.native_queue_for_presentation(), swapChainDescriptor, *m_assertContext,
            cue::default_d3d12_swap_chain_native_functions(), failureHandler);

        if (!swapChainResult)
        {
            return cue::Result<std::unique_ptr<cue::PresentationContext>>::failure(
                std::move(*swapChainResult.try_error()));
        }

        cue::D3d12SwapChainState swapChain = std::move(*swapChainResult.try_value());
        cue::D3d12RtvHeapFailureHandler rtvFailureHandler = {
            this,
            handle_rtv_heap_native_failure,
        };
        cue::Result<cue::D3d12RtvHeap> rtvHeapResult = cue::create_d3d12_rtv_heap(
            m_device.Get(), *m_assertContext, cue::default_d3d12_rtv_heap_native_functions(), rtvFailureHandler);

        if (!rtvHeapResult)
        {
            cue::Result<void> swapChainShutdownResult = swapChain.shutdown();
            cue::Error failureError = std::move(*rtvHeapResult.try_error());

            if (!swapChainShutdownResult)
            {
                add_secondary_error_context(failureError, *swapChainShutdownResult.try_error(),
                                            "D3D12 Swap Chain rollback also failed", *m_assertContext);
            }

            return cue::Result<std::unique_ptr<cue::PresentationContext>>::failure(std::move(failureError));
        }

        try
        {
            std::unique_ptr<cue::PresentationContext> presentation = std::make_unique<D3d12PresentationContext>(
                std::move(swapChain), std::move(*rtvHeapResult.try_value()), *this, this, unregister_presentation,
                prepare_device_removed_cleanup, *m_assertContext);
            ++m_activePresentationCount;
            return cue::Result<std::unique_ptr<cue::PresentationContext>>::success(std::move(presentation));
        }
        catch (...)
        {
            terminate_allocation(*m_assertContext);
        }
    }

    [[nodiscard]] cue::Result<void> classify_presentation_native_failure(cue::Error &&a_error) noexcept
    {
        cue::Result<void> classificationResult = m_queueState.reclassify_device_failure(std::move(a_error));

        if (classificationResult)
        {
            m_assertContext->fatal_handler().terminate("D3D12 native failure classification did not retain an Error");
        }

        std::optional<cue::Error> firstError;
        firstError.emplace(std::move(*classificationResult.try_error()));

        if (m_queueState.status() == cue::D3d12QueueStateStatus::DeviceRemoved)
        {
            m_state = cue::GraphicsBackendState::DeviceRemoved;
            cue::Result<void> dredResult = ensure_device_removed_diagnostics();
            retain_shutdown_error(firstError, dredResult,
                                  "D3D12 DRED diagnostics also failed during Presentation creation", *m_assertContext);
        }

        return cue::Result<void>::failure(std::move(*firstError));
    }

    [[nodiscard]] static cue::Result<void> handle_swap_chain_native_failure(
        void *a_backend, cue::Error &&a_error, const cue::D3d12SwapChainFailureResources &a_resources) noexcept
    {
        static_cast<void>(a_resources);
        D3d12BackendImpl &backend = *static_cast<D3d12BackendImpl *>(a_backend);
        CUE_ASSERT(*backend.m_assertContext, std::this_thread::get_id() == backend.m_creationThread,
                   "D3D12 Swap Chain failure must be classified on the Backend creation thread");
        return backend.classify_presentation_native_failure(std::move(a_error));
    }

    [[nodiscard]] static cue::Result<void> handle_rtv_heap_native_failure(
        void *a_backend, cue::Error &&a_error, const cue::D3d12RtvHeapFailureResources &a_resources) noexcept
    {
        static_cast<void>(a_resources);
        D3d12BackendImpl &backend = *static_cast<D3d12BackendImpl *>(a_backend);
        CUE_ASSERT(*backend.m_assertContext, std::this_thread::get_id() == backend.m_creationThread,
                   "D3D12 RTV Heap failure must be classified on the Backend creation thread");
        return backend.classify_presentation_native_failure(std::move(a_error));
    }

    static void unregister_presentation(void *a_backend) noexcept
    {
        D3d12BackendImpl &backend = *static_cast<D3d12BackendImpl *>(a_backend);
        CUE_ASSERT(*backend.m_assertContext, std::this_thread::get_id() == backend.m_creationThread,
                   "D3D12 Presentation must be unregistered on the Backend creation thread");
        CUE_ASSERT(*backend.m_assertContext, backend.m_activePresentationCount > 0,
                   "D3D12 Presentation registration count underflow");
        --backend.m_activePresentationCount;
    }

    [[nodiscard]] static cue::Result<void> prepare_device_removed_cleanup(void *a_backend) noexcept
    {
        D3d12BackendImpl &backend = *static_cast<D3d12BackendImpl *>(a_backend);
        CUE_ASSERT(*backend.m_assertContext, std::this_thread::get_id() == backend.m_creationThread,
                   "D3D12 Device Removal diagnostics must run on the Backend creation thread");
        return backend.ensure_device_removed_diagnostics();
    }

    Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    cue::D3d12QueueState m_queueState;
    cue::CapabilityReport m_capabilities;
    cue::D3d12DiagnosticsStatus m_diagnostics;
    cue::AssertContext *m_assertContext;
    std::thread::id m_creationThread;
    cue::GraphicsBackendState m_state;
    std::uint32_t m_activePresentationCount;
    std::uint32_t m_dredCollectionAttemptCount;
};
} // namespace

namespace cue
{
D3d12Backend::~D3d12Backend() noexcept = default;

Result<void> force_d3d12_device_removal_for_probe(D3d12Backend &a_backend) noexcept
{
    D3d12BackendImpl *backend = dynamic_cast<D3d12BackendImpl *>(&a_backend);

    if (backend == nullptr)
    {
        return Result<void>::failure(make_error(a_backend.assert_context_for_presentation(),
                                                k_deviceRemovalProbeUnavailable,
                                                "D3D12 Device Removal probe requires the production Backend"));
    }

    return backend->force_device_removal_for_probe();
}

Result<std::uint32_t> d3d12_dred_attempt_count_for_probe(D3d12Backend &a_backend) noexcept
{
    D3d12BackendImpl *backend = dynamic_cast<D3d12BackendImpl *>(&a_backend);

    if (backend == nullptr)
    {
        return Result<std::uint32_t>::failure(make_error(a_backend.assert_context_for_presentation(),
                                                         k_deviceRemovalProbeUnavailable,
                                                         "D3D12 DRED attempt probe requires the production Backend"));
    }

    std::uint32_t attemptCount = backend->dred_attempt_count_for_probe();
    return Result<std::uint32_t>::success(std::move(attemptCount));
}

Result<std::unique_ptr<D3d12Backend>> create_d3d12_backend(const D3d12BackendDescriptor &a_descriptor,
                                                           AssertContext &a_assertContext) noexcept
{
    if (a_descriptor.gpuWaitTimeoutMilliseconds == 0 ||
        a_descriptor.gpuWaitTimeoutMilliseconds > k_maximumWaitTimeoutMilliseconds)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(make_error(
            a_assertContext, k_invalidWaitTimeout, "D3D12 GPU Wait Timeout must be between 1 and 60000 milliseconds"));
    }

    Result<D3d12DiagnosticsStatus> diagnosticsResult =
        configure_d3d12_pre_device_diagnostics(a_descriptor, a_assertContext);

    if (!diagnosticsResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(std::move(*diagnosticsResult.try_error()));
    }

    D3d12DiagnosticsStatus diagnostics = *diagnosticsResult.try_value();
    Result<D3d12AdapterSelection> selectionResult =
        select_d3d12_adapter(a_descriptor.adapterPolicy, diagnostics, a_assertContext);

    if (!selectionResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(std::move(*selectionResult.try_error()));
    }

    D3d12AdapterSelection selection = std::move(*selectionResult.try_value());
    Result<Microsoft::WRL::ComPtr<ID3D12Device>> deviceResult =
        create_d3d12_device(selection.adapter.Get(), selection.featureLevel, a_assertContext);

    if (!deviceResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(std::move(*deviceResult.try_error()));
    }

    Microsoft::WRL::ComPtr<ID3D12Device> device = std::move(*deviceResult.try_value());
    HRESULT nameResult = device->SetName(L"CueEngine D3D12 Device");

    if (FAILED(nameResult))
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(make_native_error(
            a_assertContext, k_deviceNameFailed, "D3D12 Device diagnostic name could not be set", nameResult));
    }

    Result<void> infoQueueResult = configure_d3d12_info_queue(device.Get(), diagnostics, a_assertContext);

    if (!infoQueueResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(std::move(*infoQueueResult.try_error()));
    }

    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture = {};
    architecture.NodeIndex = 0;
    HRESULT capabilityResult =
        device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture));

    if (FAILED(capabilityResult))
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(
            make_native_error(a_assertContext, k_capabilityQueryFailed,
                              "D3D12 Device architecture capability could not be queried", capabilityResult));
    }

    Result<CapabilityReport> capabilityReportResult =
        make_d3d12_capability_report(selection.report, architecture.UMA != FALSE, a_assertContext);

    if (!capabilityReportResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(std::move(*capabilityReportResult.try_error()));
    }

    Result<D3d12QueueState> queueStateResult =
        create_d3d12_queue_state(device.Get(), a_descriptor.gpuWaitTimeoutMilliseconds, a_assertContext);

    if (!queueStateResult)
    {
        return Result<std::unique_ptr<D3d12Backend>>::failure(std::move(*queueStateResult.try_error()));
    }

    try
    {
        std::unique_ptr<D3d12Backend> backend = std::make_unique<D3d12BackendImpl>(
            std::move(selection), std::move(device), std::move(*queueStateResult.try_value()), diagnostics,
            std::move(*capabilityReportResult.try_value()), a_assertContext);
        return Result<std::unique_ptr<D3d12Backend>>::success(std::move(backend));
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}
} // namespace cue
