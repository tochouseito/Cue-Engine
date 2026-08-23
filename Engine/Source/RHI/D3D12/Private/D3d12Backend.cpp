#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/D3D12/TestSupport/D3d12SwapChainProbe.h>

#include "D3d12AdapterSelection.h"
#include "D3d12DeviceCreation.h"
#include "D3d12Diagnostics.h"
#include "D3d12FrameCommandState.h"
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
constexpr std::int64_t k_backBufferRtvMismatch = 91;
constexpr std::int64_t k_backBufferRtvCreationFailed = 92;
constexpr std::int64_t k_resizeNotAtFrameBoundary = 93;
constexpr std::uint32_t k_maximumWaitTimeoutMilliseconds = 60000;

struct RtvCreationProbeState final
{
    bool isRebuildFailureEnabled;
    std::uint32_t callCount;
};

thread_local RtvCreationProbeState g_rtvCreationProbeState = {};

struct QueueLifecycleProbeState final
{
    bool useProbeFunctions;
    bool failSignalAfterForwarding;
    bool failWaitWithoutCompletion;
};

thread_local QueueLifecycleProbeState g_queueLifecycleProbeState = {};

void execute_command_lists_for_lifecycle_probe(ID3D12CommandQueue *a_queue, UINT a_count,
                                                ID3D12CommandList *const *a_lists) noexcept
{
    cue::default_d3d12_queue_native_functions().executeCommandLists(a_queue, a_count, a_lists);
}

HRESULT signal_for_lifecycle_probe(ID3D12CommandQueue *a_queue, ID3D12Fence *a_fence,
                                   std::uint64_t a_value) noexcept
{
    const HRESULT result = cue::default_d3d12_queue_native_functions().signal(a_queue, a_fence, a_value);
    return SUCCEEDED(result) && g_queueLifecycleProbeState.failSignalAfterForwarding ? E_FAIL : result;
}

std::uint64_t completed_value_for_lifecycle_probe(ID3D12Fence *a_fence) noexcept
{
    if (g_queueLifecycleProbeState.failWaitWithoutCompletion)
    {
        return 0;
    }

    return cue::default_d3d12_queue_native_functions().getCompletedValue(a_fence);
}

HRESULT set_event_for_lifecycle_probe(ID3D12Fence *a_fence, std::uint64_t a_value, HANDLE a_event) noexcept
{
    return cue::default_d3d12_queue_native_functions().setEventOnCompletion(a_fence, a_value, a_event);
}

DWORD WINAPI wait_for_lifecycle_probe(HANDLE a_event, DWORD a_timeout)
{
    if (g_queueLifecycleProbeState.failWaitWithoutCompletion)
    {
        return WAIT_TIMEOUT;
    }

    return cue::default_d3d12_queue_native_functions().waitForSingleObject(a_event, a_timeout);
}

[[nodiscard]] cue::D3d12QueueNativeFunctions make_queue_lifecycle_probe_functions() noexcept
{
    cue::D3d12QueueNativeFunctions functions = cue::default_d3d12_queue_native_functions();
    functions.executeCommandLists = execute_command_lists_for_lifecycle_probe;
    functions.signal = signal_for_lifecycle_probe;
    functions.getCompletedValue = completed_value_for_lifecycle_probe;
    functions.setEventOnCompletion = set_event_for_lifecycle_probe;
    functions.waitForSingleObject = wait_for_lifecycle_probe;
    return functions;
}

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

struct PresentationCleanupOwnerShape final
{
    std::uint32_t allocatorCount;
    std::uint32_t backBufferCount;
    std::uint32_t rtvCount;
    bool hasCommandList;
    bool hasSwapChain;
    bool hasRtvHeap;
};

using PreparePresentationCleanup = cue::Result<void> (*)(void *, const PresentationCleanupOwnerShape &) noexcept;

[[nodiscard]] HRESULT create_render_target_view(ID3D12Device *a_device, ID3D12Resource *a_resource,
                                                const D3D12_RENDER_TARGET_VIEW_DESC *a_descriptor,
                                                D3D12_CPU_DESCRIPTOR_HANDLE a_handle) noexcept
{
    ++g_rtvCreationProbeState.callCount;

    if (g_rtvCreationProbeState.isRebuildFailureEnabled &&
        g_rtvCreationProbeState.callCount > cue::k_d3d12SwapChainBufferCount)
    {
        return E_FAIL;
    }

    a_device->CreateRenderTargetView(a_resource, a_descriptor, a_handle);
    return S_OK;
}

[[nodiscard]] cue::Result<void> bind_swap_chain_back_buffers(cue::D3d12SwapChainState &a_swapChain,
                                                              cue::D3d12FrameCommandState &a_frameState) noexcept
{
    cue::Result<cue::D3d12SwapChainBackBuffers> backBuffersResult = a_swapChain.take_back_buffers();

    if (!backBuffersResult)
    {
        return cue::Result<void>::failure(std::move(*backBuffersResult.try_error()));
    }

    return a_frameState.bind_back_buffers(std::move(*backBuffersResult.try_value()));
}

[[nodiscard]] cue::Result<void> create_back_buffer_rtvs(ID3D12Device *a_device,
                                                         cue::D3d12FrameCommandState &a_frameState,
                                                         DXGI_FORMAT a_format, cue::D3d12RtvHeap &a_heap,
                                                         const cue::AssertContext &a_assertContext) noexcept
{
    CUE_ASSERT(a_assertContext, a_device != nullptr, "D3D12 Back Buffer RTV creation requires a Device");
    CUE_ASSERT(a_assertContext, a_heap.used_count() == 0, "D3D12 Back Buffer RTV creation requires an empty Heap");

    for (std::uint32_t index = 0; index < cue::k_d3d12SwapChainBufferCount; ++index)
    {
        cue::Result<ID3D12Resource *> backBufferResult = a_frameState.back_buffer(index);

        if (!backBufferResult)
        {
            static_cast<void>(a_frameState.release_rtv_slots(a_heap));
            return cue::Result<void>::failure(std::move(*backBufferResult.try_error()));
        }

        ID3D12Resource *backBuffer = *backBufferResult.try_value();
        const D3D12_RESOURCE_DESC resourceDescriptor = backBuffer->GetDesc();

        if (resourceDescriptor.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            resourceDescriptor.Format != a_format)
        {
            static_cast<void>(a_frameState.release_rtv_slots(a_heap));
            return cue::Result<void>::failure(make_error(
                a_assertContext, k_backBufferRtvMismatch, "D3D12 Back Buffer and RTV Format do not match"));
        }

        cue::Result<cue::D3d12RtvSlot> slotResult = a_heap.allocate();

        if (!slotResult)
        {
            static_cast<void>(a_frameState.release_rtv_slots(a_heap));
            return cue::Result<void>::failure(std::move(*slotResult.try_error()));
        }

        cue::D3d12RtvSlot slot = *slotResult.try_value();
        cue::Result<D3D12_CPU_DESCRIPTOR_HANDLE> handleResult = a_heap.cpu_handle(slot);

        if (!handleResult)
        {
            static_cast<void>(a_heap.release(slot));
            static_cast<void>(a_frameState.release_rtv_slots(a_heap));
            return cue::Result<void>::failure(std::move(*handleResult.try_error()));
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDescriptor = {};
        rtvDescriptor.Format = a_format;
        rtvDescriptor.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDescriptor.Texture2D.MipSlice = 0;
        rtvDescriptor.Texture2D.PlaneSlice = 0;
        const HRESULT creationResult =
            create_render_target_view(a_device, backBuffer, &rtvDescriptor, *handleResult.try_value());

        if (FAILED(creationResult))
        {
            static_cast<void>(a_heap.release(slot));
            static_cast<void>(a_frameState.release_rtv_slots(a_heap));
            return cue::Result<void>::failure(make_native_error(
                a_assertContext, k_backBufferRtvCreationFailed, "D3D12 Back Buffer RTV creation failed",
                creationResult));
        }

        cue::Result<void> bindingResult = a_frameState.bind_rtv_slot(index, slot);

        if (!bindingResult)
        {
            static_cast<void>(a_heap.release(slot));
            static_cast<void>(a_frameState.release_rtv_slots(a_heap));
            return bindingResult;
        }
    }

    return cue::Result<void>::success();
}

class D3d12PresentationContext final : public cue::PresentationContext
{
  public:
    D3d12PresentationContext(cue::D3d12SwapChainState &&a_swapChain,
                             cue::D3d12FrameCommandState &&a_frameCommandState, cue::D3d12RtvHeap &&a_rtvHeap,
                             ID3D12Device *a_device,
                             cue::GraphicsBackend &a_backend, void *a_backendOwner,
                             UnregisterPresentation a_unregisterPresentation,
                             PreparePresentationCleanup a_preparePresentationCleanup,
                             cue::AssertContext &a_assertContext) noexcept
        : m_swapChain(std::move(a_swapChain)), m_frameCommandState(std::move(a_frameCommandState)),
          m_rtvHeap(std::move(a_rtvHeap)), m_device(a_device),
          m_backend(&a_backend), m_backendOwner(a_backendOwner), m_unregisterPresentation(a_unregisterPresentation),
          m_preparePresentationCleanup(a_preparePresentationCleanup), m_assertContext(&a_assertContext),
          m_creationThread(std::this_thread::get_id()), m_state(cue::PresentationContextState::Ready),
          m_isRegistered(true), m_isResizePending(false)
    {
    }

    ~D3d12PresentationContext() noexcept override
    {
        CUE_ASSERT(*m_assertContext, std::this_thread::get_id() == m_creationThread,
                   "D3D12 Presentation Context must be destroyed on the creation thread");

        if (m_state != cue::PresentationContextState::Shutdown || m_isRegistered ||
            m_frameCommandState.has_native_objects() || m_swapChain.has_native_objects() ||
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

    [[nodiscard]] bool is_resize_pending() const noexcept override
    {
        assert_thread("D3D12 Presentation Resize pending state must be queried on the creation thread");
        return m_isResizePending;
    }

    [[nodiscard]] cue::Result<void> resize(std::uint32_t a_width, std::uint32_t a_height) noexcept override
    {
        assert_thread("D3D12 Presentation Resize must run on the creation thread");

        if (m_state != cue::PresentationContextState::Ready ||
            m_backend->state() != cue::GraphicsBackendState::Ready)
        {
            return cue::Result<void>::failure(
                make_error(*m_assertContext, k_presentationUnavailable, "D3D12 Presentation Resize is unavailable"));
        }

        const cue::D3d12CommandListState commandListState = m_frameCommandState.command_list_state();

        if (commandListState != cue::D3d12CommandListState::IdleClosed &&
            commandListState != cue::D3d12CommandListState::Submitted)
        {
            return cue::Result<void>::failure(make_error(
                *m_assertContext, k_resizeNotAtFrameBoundary, "D3D12 Presentation Resize requires a Frame boundary"));
        }

        if (a_width == 0 || a_height == 0)
        {
            if (m_isResizePending)
            {
                return cue::Result<void>::success();
            }

            cue::Result<void> suspendResult = m_frameCommandState.suspend_for_resize();

            if (!suspendResult)
            {
                return suspendResult;
            }

            m_isResizePending = true;
            return cue::Result<void>::success();
        }

        if (a_width == m_swapChain.width() && a_height == m_swapChain.height())
        {
            if (!m_isResizePending && !m_frameCommandState.is_accepting_frames())
            {
                return cue::Result<void>::failure(make_error(
                    *m_assertContext, k_presentationUnavailable,
                    "D3D12 Presentation Frame acceptance was stopped before same-size Resize"));
            }

            if (m_isResizePending)
            {
                cue::Result<void> resumeResult = m_frameCommandState.resume_after_resize();

                if (!resumeResult)
                {
                    return resumeResult;
                }
            }

            m_isResizePending = false;
            return cue::Result<void>::success();
        }

        cue::Result<void> prepareResult =
            m_frameCommandState.prepare_for_resize(m_swapChain.current_back_buffer_index());

        if (!prepareResult)
        {
            cue::Error prepareError = std::move(*prepareResult.try_error());
            cue::Result<void> cleanupPreparation = prepare_backend_cleanup();

            if (!cleanupPreparation)
            {
                add_secondary_error_context(prepareError, *cleanupPreparation.try_error(),
                                            "D3D12 Device Removal diagnostics also failed after Resize preparation",
                                            *m_assertContext);
            }

            if (m_backend->state() == cue::GraphicsBackendState::DeviceRemoved)
            {
                return shutdown_after_resize_failure(std::move(prepareError), true);
            }

            if (m_frameCommandState.status() == cue::D3d12FrameCommandStatus::Unavailable ||
                m_backend->state() == cue::GraphicsBackendState::Unavailable)
            {
                m_state = cue::PresentationContextState::Unavailable;
                return cue::Result<void>::failure(std::move(prepareError));
            }

            if (m_frameCommandState.was_resize_gpu_idle_proven())
            {
                return shutdown_after_resize_failure(std::move(prepareError), false);
            }

            return cue::Result<void>::failure(std::move(prepareError));
        }

        cue::Result<void> resourceReleasePreparation = prepare_backend_cleanup();

        if (m_backend->state() == cue::GraphicsBackendState::DeviceRemoved)
        {
            const HRESULT removalReason = m_device->GetDeviceRemovedReason();
            cue::Error removalError = make_native_error(
                *m_assertContext, k_deviceRemoved, "D3D12 Device was removed before Resize resource release",
                removalReason);

            if (!resourceReleasePreparation)
            {
                add_secondary_error_context(removalError, *resourceReleasePreparation.try_error(),
                                            "D3D12 Device Removal diagnostics also failed before Resize cleanup",
                                            *m_assertContext);
            }

            return shutdown_after_resize_failure(std::move(removalError), true);
        }

        if (!resourceReleasePreparation)
        {
            return shutdown_after_resize_failure(std::move(*resourceReleasePreparation.try_error()), false);
        }

        cue::Result<void> rtvReleaseResult = m_frameCommandState.release_rtv_slots(m_rtvHeap);

        if (!rtvReleaseResult)
        {
            return shutdown_after_resize_failure(std::move(*rtvReleaseResult.try_error()), false);
        }

        m_frameCommandState.release_back_buffers();

        cue::Result<void> resizeResult = m_swapChain.resize(a_width, a_height);

        if (!resizeResult)
        {
            static_cast<void>(prepare_backend_cleanup());
            const bool isDeviceRemoved = m_backend->state() == cue::GraphicsBackendState::DeviceRemoved;
            return shutdown_after_resize_failure(std::move(*resizeResult.try_error()), isDeviceRemoved);
        }

        cue::Result<void> bindingResult = bind_swap_chain_back_buffers(m_swapChain, m_frameCommandState);

        if (!bindingResult)
        {
            return shutdown_after_resize_failure(std::move(*bindingResult.try_error()), false);
        }

        cue::Result<void> rtvResult =
            create_back_buffer_rtvs(m_device, m_frameCommandState, m_swapChain.format(), m_rtvHeap, *m_assertContext);

        if (!rtvResult)
        {
            return shutdown_after_resize_failure(std::move(*rtvResult.try_error()), false);
        }

        cue::Result<void> resumeResult = m_frameCommandState.resume_after_resize();

        if (!resumeResult)
        {
            return shutdown_after_resize_failure(std::move(*resumeResult.try_error()), false);
        }

        m_isResizePending = false;
        return cue::Result<void>::success();
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

        std::optional<cue::Error> firstError;
        cue::Result<void> diagnosticsResult = prepare_backend_cleanup();
        retain_shutdown_error(firstError, diagnosticsResult,
                              "D3D12 Device Removal diagnostics failed before Presentation cleanup",
                              *m_assertContext);

        if (m_backend->state() == cue::GraphicsBackendState::DeviceRemoved)
        {
            m_state = cue::PresentationContextState::DeviceRemoved;
        }

        cue::Result<void> frameResult = m_backend->state() == cue::GraphicsBackendState::DeviceRemoved
                                             ? m_frameCommandState.begin_release_after_device_removed()
                                             : m_frameCommandState.begin_shutdown();
        retain_shutdown_error(firstError, frameResult, "D3D12 Frame Command cleanup also failed", *m_assertContext);

        if (m_frameCommandState.status() == cue::D3d12FrameCommandStatus::DeviceRemoved)
        {
            cue::Result<void> removalPreparation = prepare_backend_cleanup();
            retain_shutdown_error(firstError, removalPreparation,
                                  "D3D12 Device Removal diagnostics also failed during Presentation shutdown",
                                  *m_assertContext);
            cue::Result<void> releaseResult = m_frameCommandState.begin_release_after_device_removed();
            retain_shutdown_error(firstError, releaseResult,
                                  "D3D12 Frame Command Device Removal cleanup also failed", *m_assertContext);
        }

        if (m_frameCommandState.status() == cue::D3d12FrameCommandStatus::Unavailable)
        {
            m_state = cue::PresentationContextState::Unavailable;
            return cue::Result<void>::failure(std::move(*firstError));
        }

        cue::Result<void> resourceReleasePreparation = prepare_backend_cleanup();
        retain_shutdown_error(firstError, resourceReleasePreparation,
                              "D3D12 Device Removal diagnostics failed immediately before Presentation resource release",
                              *m_assertContext);

        if (m_backend->state() == cue::GraphicsBackendState::DeviceRemoved)
        {
            m_state = cue::PresentationContextState::DeviceRemoved;
            cue::Result<void> releaseResult = m_frameCommandState.begin_release_after_device_removed();
            retain_shutdown_error(firstError, releaseResult,
                                  "D3D12 Frame Command Device Removal cleanup also failed before resource release",
                                  *m_assertContext);
        }

        if (m_frameCommandState.status() != cue::D3d12FrameCommandStatus::CleanupPending)
        {
            return cue::Result<void>::failure(std::move(*firstError));
        }

        cue::Result<void> rtvReleaseResult = m_frameCommandState.release_rtv_slots(m_rtvHeap);
        m_frameCommandState.release_back_buffers();
        cue::Result<void> allocatorResult =
            m_frameCommandState.release_allocators_after_presentation_cleanup();
        cue::Result<void> rtvHeapResult = m_rtvHeap.shutdown();
        cue::Result<void> swapChainResult = m_swapChain.shutdown();
        retain_shutdown_error(firstError, rtvReleaseResult, "D3D12 RTV Slot cleanup also failed", *m_assertContext);
        retain_shutdown_error(firstError, allocatorResult, "D3D12 Frame Allocator cleanup also failed",
                              *m_assertContext);
        retain_shutdown_error(firstError, rtvHeapResult, "D3D12 RTV Heap cleanup also failed", *m_assertContext);
        retain_shutdown_error(firstError, swapChainResult, "D3D12 Swap Chain cleanup also failed", *m_assertContext);

        if (!rtvReleaseResult || !allocatorResult || !swapChainResult || !rtvHeapResult)
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

    [[nodiscard]] cue::D3d12PresentationProbeReport probe_report() const noexcept
    {
        cue::D3d12PresentationProbeReport report = {};
        report.formatsMatch = true;

        for (std::uint32_t index = 0; index < cue::k_d3d12SwapChainBufferCount; ++index)
        {
            if (m_frameCommandState.rtv_count() != cue::k_d3d12SwapChainBufferCount)
            {
                report.formatsMatch = false;
                break;
            }

            cue::Result<ID3D12Resource *> backBufferResult = m_frameCommandState.back_buffer(index);

            if (!backBufferResult || (*backBufferResult.try_value())->GetDesc().Format != m_swapChain.format())
            {
                report.formatsMatch = false;
            }
        }

        report.allocatorCount = m_frameCommandState.allocator_count();
        report.backBufferCount = m_frameCommandState.back_buffer_count();
        report.rtvCount = m_frameCommandState.rtv_count();
        report.formatsMatch = report.formatsMatch && m_frameCommandState.are_back_buffers_present();
        report.isAcceptingFrames = m_frameCommandState.is_accepting_frames();
        report.hasCommandList = m_frameCommandState.has_command_list();
        report.hasSwapChain = m_swapChain.has_native_objects();
        report.hasRtvHeap = m_rtvHeap.has_native_object();
        report.isRegistered = m_isRegistered;
        return report;
    }

    [[nodiscard]] cue::Result<std::uint64_t> submit_transition_frame_for_probe() noexcept
    {
        const std::uint32_t frameIndex = m_swapChain.current_back_buffer_index();
        cue::Result<void> beginResult = m_frameCommandState.begin_frame(frameIndex);

        if (!beginResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*beginResult.try_error()));
        }

        cue::Result<void> renderTargetResult =
            m_frameCommandState.transition_back_buffer(frameIndex, cue::D3d12BackBufferState::RenderTarget);

        if (!renderTargetResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*renderTargetResult.try_error()));
        }

        cue::Result<void> presentStateResult =
            m_frameCommandState.transition_back_buffer(frameIndex, cue::D3d12BackBufferState::Present);

        if (!presentStateResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*presentStateResult.try_error()));
        }

        cue::Result<void> closeResult = m_frameCommandState.close_frame();

        if (!closeResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
        }

        cue::Result<void> executeResult = m_frameCommandState.execute_frame();

        if (!executeResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*executeResult.try_error()));
        }

        cue::Result<void> presentResult = m_frameCommandState.mark_present_attempted();

        if (!presentResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*presentResult.try_error()));
        }

        return m_frameCommandState.signal_frame();
    }

    [[nodiscard]] cue::Result<std::uint64_t> submit_clear_frame_for_probe(
        const std::array<float, 4> &a_color) noexcept
    {
        const std::uint32_t frameIndex = m_swapChain.current_back_buffer_index();
        cue::Result<void> beginResult = m_frameCommandState.begin_frame(frameIndex);

        if (!beginResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*beginResult.try_error()));
        }

        cue::Result<void> renderTargetResult =
            m_frameCommandState.transition_back_buffer(frameIndex, cue::D3d12BackBufferState::RenderTarget);

        if (!renderTargetResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*renderTargetResult.try_error()));
        }

        cue::Result<void> clearResult = m_frameCommandState.clear_back_buffer(frameIndex, m_rtvHeap, a_color);

        if (!clearResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*clearResult.try_error()));
        }

        cue::Result<void> presentStateResult =
            m_frameCommandState.transition_back_buffer(frameIndex, cue::D3d12BackBufferState::Present);

        if (!presentStateResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*presentStateResult.try_error()));
        }

        cue::Result<void> closeResult = m_frameCommandState.close_frame();

        if (!closeResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*closeResult.try_error()));
        }

        cue::Result<void> executeResult = m_frameCommandState.execute_frame();

        if (!executeResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*executeResult.try_error()));
        }

        cue::Result<void> presentResult = m_frameCommandState.mark_present_attempted();

        if (!presentResult)
        {
            return cue::Result<std::uint64_t>::failure(std::move(*presentResult.try_error()));
        }

        return m_frameCommandState.signal_frame();
    }

  private:
    [[nodiscard]] cue::Result<void> prepare_backend_cleanup() noexcept
    {
        PresentationCleanupOwnerShape shape = {
            m_frameCommandState.allocator_count(),
            m_frameCommandState.back_buffer_count(),
            m_frameCommandState.rtv_count(),
            m_frameCommandState.has_command_list(),
            m_swapChain.has_native_objects(),
            m_rtvHeap.has_native_object(),
        };
        return m_preparePresentationCleanup(m_backendOwner, shape);
    }

    [[nodiscard]] cue::Result<void> shutdown_after_resize_failure(cue::Error &&a_error,
                                                                  bool a_isDeviceRemoved) noexcept
    {
        std::optional<cue::Error> firstError;
        firstError.emplace(std::move(a_error));
        cue::Result<void> frameResult = a_isDeviceRemoved
                                            ? m_frameCommandState.begin_release_after_device_removed()
                                            : m_frameCommandState.begin_release_after_gpu_idle();
        retain_shutdown_error(firstError, frameResult, "D3D12 Frame Command cleanup also failed", *m_assertContext);

        if (m_frameCommandState.status() != cue::D3d12FrameCommandStatus::CleanupPending)
        {
            m_state = cue::PresentationContextState::Unavailable;
            return cue::Result<void>::failure(std::move(*firstError));
        }

        cue::Result<void> rtvReleaseResult = m_frameCommandState.release_rtv_slots(m_rtvHeap);
        m_frameCommandState.release_back_buffers();
        cue::Result<void> allocatorResult =
            m_frameCommandState.release_allocators_after_presentation_cleanup();
        cue::Result<void> rtvHeapResult = m_rtvHeap.shutdown();
        cue::Result<void> swapChainResult = m_swapChain.shutdown();
        retain_shutdown_error(firstError, rtvReleaseResult, "D3D12 RTV Slot cleanup also failed", *m_assertContext);
        retain_shutdown_error(firstError, allocatorResult, "D3D12 Frame Allocator cleanup also failed",
                              *m_assertContext);
        retain_shutdown_error(firstError, rtvHeapResult, "D3D12 RTV Heap cleanup also failed", *m_assertContext);
        retain_shutdown_error(firstError, swapChainResult, "D3D12 Swap Chain cleanup also failed", *m_assertContext);

        if (!rtvReleaseResult || !allocatorResult || !rtvHeapResult || !swapChainResult)
        {
            m_state = cue::PresentationContextState::Unavailable;
            return cue::Result<void>::failure(std::move(*firstError));
        }

        unregister_from_backend();
        m_state = cue::PresentationContextState::Shutdown;
        return cue::Result<void>::failure(std::move(*firstError));
    }

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
    cue::D3d12FrameCommandState m_frameCommandState;
    cue::D3d12RtvHeap m_rtvHeap;
    ID3D12Device *m_device;
    cue::GraphicsBackend *m_backend;
    void *m_backendOwner;
    UnregisterPresentation m_unregisterPresentation;
    PreparePresentationCleanup m_preparePresentationCleanup;
    cue::AssertContext *m_assertContext;
    std::thread::id m_creationThread;
    cue::PresentationContextState m_state;
    bool m_isRegistered;
    bool m_isResizePending;
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
          m_activePresentationCount(0), m_dredCollectionAttemptCount(0), m_lastDredOwnerReport{}
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
        cue::Result<void> removalResult = remove_device_without_classification_for_probe();

        if (!removalResult)
        {
            return removalResult;
        }

        cue::Error removalError = make_native_error(
            *m_assertContext, k_deviceRemoved, "D3D12 Device Removal probe was requested", DXGI_ERROR_DEVICE_REMOVED);
        return classify_presentation_native_failure(std::move(removalError));
    }

    [[nodiscard]] cue::Result<void> remove_device_without_classification_for_probe() noexcept
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
        return cue::Result<void>::success();
    }

    [[nodiscard]] std::uint32_t dred_attempt_count_for_probe() const noexcept
    {
        return m_dredCollectionAttemptCount;
    }

    [[nodiscard]] cue::D3d12BackendOwnerProbeReport owner_report_for_probe() const noexcept
    {
        return {
            m_queueState.has_queue(),
            m_queueState.has_fence(),
            m_queueState.has_fence_event(),
            m_device != nullptr,
            m_adapter != nullptr,
            m_factory != nullptr,
        };
    }

    [[nodiscard]] cue::D3d12DredOwnerProbeReport dred_owner_report_for_probe() const noexcept
    {
        return m_lastDredOwnerReport;
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
    [[nodiscard]] cue::Result<void> ensure_device_removed_diagnostics(
        const PresentationCleanupOwnerShape *a_presentationShape = nullptr) noexcept
    {
        if (m_dredCollectionAttemptCount != 0)
        {
            return cue::Result<void>::success();
        }

        if (a_presentationShape != nullptr)
        {
            m_lastDredOwnerReport = {
                a_presentationShape->allocatorCount,
                a_presentationShape->backBufferCount,
                a_presentationShape->rtvCount,
                a_presentationShape->hasCommandList,
                a_presentationShape->hasSwapChain,
                a_presentationShape->hasRtvHeap,
                m_queueState.has_queue(),
                m_queueState.has_fence(),
                m_queueState.has_fence_event(),
            };
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

        cue::D3d12RtvHeap rtvHeap = std::move(*rtvHeapResult.try_value());
        cue::Result<cue::D3d12FrameCommandState> frameStateResult = cue::create_d3d12_frame_command_state(
            m_device.Get(), m_queueState, *m_assertContext, cue::default_d3d12_frame_command_native_functions());

        if (!frameStateResult)
        {
            cue::Error failureError = std::move(*frameStateResult.try_error());
            cue::Result<void> heapShutdownResult = rtvHeap.shutdown();
            cue::Result<void> swapChainShutdownResult = swapChain.shutdown();

            if (!heapShutdownResult)
            {
                add_secondary_error_context(failureError, *heapShutdownResult.try_error(),
                                            "D3D12 RTV Heap rollback also failed", *m_assertContext);
            }

            if (!swapChainShutdownResult)
            {
                add_secondary_error_context(failureError, *swapChainShutdownResult.try_error(),
                                            "D3D12 Swap Chain rollback also failed", *m_assertContext);
            }

            return cue::Result<std::unique_ptr<cue::PresentationContext>>::failure(std::move(failureError));
        }

        cue::D3d12FrameCommandState frameState = std::move(*frameStateResult.try_value());
        cue::Result<void> bindingResult = bind_swap_chain_back_buffers(swapChain, frameState);
        cue::Result<void> rtvResult = bindingResult
                                          ? create_back_buffer_rtvs(m_device.Get(), frameState, swapChain.format(),
                                                                    rtvHeap, *m_assertContext)
                                          : cue::Result<void>::failure(std::move(*bindingResult.try_error()));

        if (!rtvResult)
        {
            cue::Error failureError = std::move(*rtvResult.try_error());
            static_cast<void>(frameState.suspend_for_resize());
            cue::Result<void> frameBeginResult = frameState.begin_release_after_gpu_idle();
            cue::Result<void> rtvReleaseResult = frameState.release_rtv_slots(rtvHeap);
            frameState.release_back_buffers();
            cue::Result<void> allocatorResult = frameState.status() == cue::D3d12FrameCommandStatus::CleanupPending
                                                    ? frameState.release_allocators_after_presentation_cleanup()
                                                    : cue::Result<void>::failure(make_error(
                                                          *m_assertContext, k_presentationUnavailable,
                                                          "D3D12 Frame rollback could not release Allocators"));
            cue::Result<void> heapShutdownResult = rtvHeap.shutdown();
            cue::Result<void> swapChainShutdownResult = swapChain.shutdown();

            if (!frameBeginResult)
            {
                add_secondary_error_context(failureError, *frameBeginResult.try_error(),
                                            "D3D12 Frame Command rollback also failed", *m_assertContext);
            }

            if (!rtvReleaseResult)
            {
                add_secondary_error_context(failureError, *rtvReleaseResult.try_error(),
                                            "D3D12 RTV Slot rollback also failed", *m_assertContext);
            }

            if (!allocatorResult)
            {
                add_secondary_error_context(failureError, *allocatorResult.try_error(),
                                            "D3D12 Frame Allocator rollback also failed", *m_assertContext);
            }

            if (!heapShutdownResult)
            {
                add_secondary_error_context(failureError, *heapShutdownResult.try_error(),
                                            "D3D12 RTV Heap rollback also failed", *m_assertContext);
            }

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
                std::move(swapChain), std::move(frameState), std::move(rtvHeap), m_device.Get(), *this, this,
                unregister_presentation, prepare_presentation_cleanup, *m_assertContext);
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

    [[nodiscard]] static cue::Result<void> prepare_presentation_cleanup(
        void *a_backend, const PresentationCleanupOwnerShape &a_shape) noexcept
    {
        D3d12BackendImpl &backend = *static_cast<D3d12BackendImpl *>(a_backend);
        CUE_ASSERT(*backend.m_assertContext, std::this_thread::get_id() == backend.m_creationThread,
                   "D3D12 Presentation cleanup preparation must run on the Backend creation thread");

        if (backend.m_queueState.status() == cue::D3d12QueueStateStatus::Unavailable)
        {
            backend.m_state = cue::GraphicsBackendState::Unavailable;
            return cue::Result<void>::failure(
                make_error(*backend.m_assertContext, k_backendUnavailable, "D3D12 Backend is unavailable"));
        }

        if (backend.m_queueState.refresh_device_removed_status())
        {
            backend.m_state = cue::GraphicsBackendState::DeviceRemoved;
        }

        if (backend.m_state != cue::GraphicsBackendState::DeviceRemoved)
        {
            return cue::Result<void>::success();
        }

        return backend.ensure_device_removed_diagnostics(&a_shape);
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
    cue::D3d12DredOwnerProbeReport m_lastDredOwnerReport;
};
} // namespace

namespace cue
{
D3d12Backend::~D3d12Backend() noexcept = default;

D3d12PresentationProbeReport probe_d3d12_presentation(PresentationContext &a_presentation) noexcept
{
    D3d12PresentationContext *presentation = dynamic_cast<D3d12PresentationContext *>(&a_presentation);

    if (presentation == nullptr)
    {
        return {};
    }

    return presentation->probe_report();
}

bool submit_d3d12_transition_frame_for_probe(PresentationContext &a_presentation) noexcept
{
    D3d12PresentationContext *presentation = dynamic_cast<D3d12PresentationContext *>(&a_presentation);

    if (presentation == nullptr)
    {
        return false;
    }

    return static_cast<bool>(presentation->submit_transition_frame_for_probe());
}

bool submit_d3d12_clear_frame_for_probe(PresentationContext &a_presentation,
                                        const std::array<float, 4> &a_color) noexcept
{
    D3d12PresentationContext *presentation = dynamic_cast<D3d12PresentationContext *>(&a_presentation);

    if (presentation == nullptr)
    {
        return false;
    }

    return static_cast<bool>(presentation->submit_clear_frame_for_probe(a_color));
}

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

Result<void> remove_d3d12_device_without_classification_for_probe(D3d12Backend &a_backend) noexcept
{
    D3d12BackendImpl *backend = dynamic_cast<D3d12BackendImpl *>(&a_backend);

    if (backend == nullptr)
    {
        return Result<void>::failure(make_error(a_backend.assert_context_for_presentation(),
                                                k_deviceRemovalProbeUnavailable,
                                                "D3D12 Device Removal probe requires the production Backend"));
    }

    return backend->remove_device_without_classification_for_probe();
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

Result<D3d12BackendOwnerProbeReport> probe_d3d12_backend_owners_for_probe(D3d12Backend &a_backend) noexcept
{
    D3d12BackendImpl *backend = dynamic_cast<D3d12BackendImpl *>(&a_backend);

    if (backend == nullptr)
    {
        return Result<D3d12BackendOwnerProbeReport>::failure(make_error(
            a_backend.assert_context_for_presentation(), k_deviceRemovalProbeUnavailable,
            "D3D12 Backend owner probe requires the production Backend"));
    }

    D3d12BackendOwnerProbeReport report = backend->owner_report_for_probe();
    return Result<D3d12BackendOwnerProbeReport>::success(std::move(report));
}

Result<D3d12DredOwnerProbeReport> probe_d3d12_dred_owners_for_probe(D3d12Backend &a_backend) noexcept
{
    D3d12BackendImpl *backend = dynamic_cast<D3d12BackendImpl *>(&a_backend);

    if (backend == nullptr)
    {
        return Result<D3d12DredOwnerProbeReport>::failure(make_error(
            a_backend.assert_context_for_presentation(), k_deviceRemovalProbeUnavailable,
            "D3D12 DRED owner probe requires the production Backend"));
    }

    D3d12DredOwnerProbeReport report = backend->dred_owner_report_for_probe();
    return Result<D3d12DredOwnerProbeReport>::success(std::move(report));
}

bool verify_d3d12_rtv_rebuild_failure_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                 std::uint32_t a_height, AssertContext &a_assertContext) noexcept
{
    struct ProbeReset final
    {
        ~ProbeReset() noexcept
        {
            g_rtvCreationProbeState = {};
        }
    } probeReset;

    static_cast<void>(probeReset);
    g_rtvCreationProbeState = {true, 0};
    D3d12BackendDescriptor backendDescriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult =
        create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return false;
    }

    std::unique_ptr<D3d12Backend> backend = std::move(*backendResult.try_value());
    PresentationDescriptor presentationDescriptor = {true};
    Result<std::unique_ptr<PresentationContext>> presentationResult = backend->create_windows_presentation(
        a_nativeWindow, a_width, a_height, presentationDescriptor);

    if (!presentationResult)
    {
        static_cast<void>(backend->shutdown());
        return false;
    }

    std::unique_ptr<PresentationContext> presentation = std::move(*presentationResult.try_value());
    Result<void> resizeResult = presentation->resize(a_width + 1, a_height + 1);
    const Error *resizeError = resizeResult.try_error();
    const NativeError *nativeError = resizeError != nullptr ? resizeError->try_native_error() : nullptr;
    const bool failureValid = !resizeResult && resizeError->code().domain() == "Cue.RHI.D3D12" &&
                              resizeError->code().value() == k_backBufferRtvCreationFailed &&
                              nativeError != nullptr && nativeError->domain() == "D3D12" &&
                              presentation->state() == PresentationContextState::Shutdown &&
                              backend->state() == GraphicsBackendState::Ready;
    Result<void> presentationShutdownResult = presentation->shutdown();
    presentation.reset();
    Result<void> backendShutdownResult = backend->shutdown();
    const bool cleanupValid = presentationShutdownResult && backendShutdownResult &&
                              backend->state() == GraphicsBackendState::Shutdown;
    backend.reset();
    return failureValid && cleanupValid;
}

bool verify_d3d12_terminal_resize_rejection_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                       std::uint32_t a_height,
                                                       AssertContext &a_assertContext) noexcept
{
    struct ProbeReset final
    {
        ~ProbeReset() noexcept
        {
            g_queueLifecycleProbeState = {};
        }
    } probeReset;

    static_cast<void>(probeReset);
    g_queueLifecycleProbeState.useProbeFunctions = true;
    D3d12BackendDescriptor backendDescriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult =
        create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return false;
    }

    std::unique_ptr<D3d12Backend> backend = std::move(*backendResult.try_value());
    PresentationDescriptor presentationDescriptor = {true};
    Result<std::unique_ptr<PresentationContext>> presentationResult = backend->create_windows_presentation(
        a_nativeWindow, a_width, a_height, presentationDescriptor);

    if (!presentationResult)
    {
        static_cast<void>(backend->shutdown());
        return false;
    }

    std::unique_ptr<PresentationContext> presentation = std::move(*presentationResult.try_value());
    D3d12PresentationContext *d3d12Presentation = dynamic_cast<D3d12PresentationContext *>(presentation.get());

    if (d3d12Presentation == nullptr)
    {
        static_cast<void>(presentation->shutdown());
        presentation.reset();
        static_cast<void>(backend->shutdown());
        return false;
    }

    g_queueLifecycleProbeState.failSignalAfterForwarding = true;
    Result<std::uint64_t> submitResult = d3d12Presentation->submit_transition_frame_for_probe();
    g_queueLifecycleProbeState.failSignalAfterForwarding = false;
    Result<void> resizeResult = presentation->resize(0, a_height);
    Result<void> sameSizeResult = presentation->resize(a_width, a_height);
    D3d12PresentationProbeReport report = d3d12Presentation->probe_report();
    const bool rejectionValid = !submitResult && !resizeResult && !sameSizeResult &&
                                resizeResult.try_error()->code().domain() == "Cue.RHI.D3D12" &&
                                resizeResult.try_error()->code().value() == 64 &&
                                presentation->state() == PresentationContextState::Ready &&
                                !presentation->is_resize_pending() && !report.isAcceptingFrames &&
                                report.hasCommandList && report.allocatorCount == k_d3d12FrameContextCount &&
                                report.backBufferCount == k_d3d12SwapChainBufferCount &&
                                report.rtvCount == k_d3d12SwapChainBufferCount && report.hasSwapChain &&
                                report.hasRtvHeap && report.isRegistered;
    Result<void> presentationShutdownResult = presentation->shutdown();
    presentation.reset();
    Result<void> backendShutdownResult = backend->shutdown();
    const bool cleanupValid = presentationShutdownResult && backendShutdownResult &&
                              backend->state() == GraphicsBackendState::Shutdown;
    backend.reset();
    return rejectionValid && cleanupValid;
}

bool verify_d3d12_resize_unavailable_retention_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                         std::uint32_t a_height,
                                                         AssertContext &a_assertContext) noexcept
{
    struct ProbeReset final
    {
        ~ProbeReset() noexcept
        {
            g_queueLifecycleProbeState = {};
        }
    } probeReset;

    static_cast<void>(probeReset);
    g_queueLifecycleProbeState.useProbeFunctions = true;
    D3d12BackendDescriptor backendDescriptor = {
        D3d12AdapterPolicy::Warp,
        D3d12ValidationMode::Disabled,
        false,
        5'000,
    };
    Result<std::unique_ptr<D3d12Backend>> backendResult =
        create_d3d12_backend(backendDescriptor, a_assertContext);

    if (!backendResult)
    {
        return false;
    }

    std::unique_ptr<D3d12Backend> backend = std::move(*backendResult.try_value());
    PresentationDescriptor presentationDescriptor = {true};
    Result<std::unique_ptr<PresentationContext>> presentationResult = backend->create_windows_presentation(
        a_nativeWindow, a_width, a_height, presentationDescriptor);

    if (!presentationResult)
    {
        static_cast<void>(backend->shutdown());
        return false;
    }

    std::unique_ptr<PresentationContext> presentation = std::move(*presentationResult.try_value());
    D3d12PresentationContext *d3d12Presentation = dynamic_cast<D3d12PresentationContext *>(presentation.get());

    if (d3d12Presentation == nullptr)
    {
        static_cast<void>(presentation->shutdown());
        presentation.reset();
        static_cast<void>(backend->shutdown());
        return false;
    }

    Result<std::uint64_t> submitResult = d3d12Presentation->submit_transition_frame_for_probe();
    g_queueLifecycleProbeState.failWaitWithoutCompletion = true;
    Result<void> resizeResult = presentation->resize(a_width + 1, a_height + 1);
    D3d12PresentationProbeReport report = d3d12Presentation->probe_report();
    Result<D3d12BackendOwnerProbeReport> backendOwnerResult =
        probe_d3d12_backend_owners_for_probe(*backend);
    Result<void> presentationShutdownResult = presentation->shutdown();
    Result<void> backendShutdownResult = backend->shutdown();
    const D3d12BackendOwnerProbeReport *backendOwners = backendOwnerResult.try_value();
    const bool retentionValid = submitResult && !resizeResult &&
                                presentation->state() == PresentationContextState::Unavailable &&
                                backend->state() == GraphicsBackendState::Unavailable && !presentationShutdownResult &&
                                !backendShutdownResult && report.hasCommandList &&
                                report.allocatorCount == k_d3d12FrameContextCount &&
                                report.backBufferCount == k_d3d12SwapChainBufferCount &&
                                report.rtvCount == k_d3d12SwapChainBufferCount && report.formatsMatch &&
                                !report.isAcceptingFrames && report.hasSwapChain && report.hasRtvHeap &&
                                report.isRegistered && backendOwners != nullptr && backendOwners->hasQueue &&
                                backendOwners->hasFence && backendOwners->hasFenceEvent && backendOwners->hasDevice &&
                                backendOwners->hasAdapter && backendOwners->hasFactory;

    static_cast<void>(presentation.release());
    static_cast<void>(backend.release());
    return retentionValid;
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

    D3d12QueueNativeFunctions queueFunctions = g_queueLifecycleProbeState.useProbeFunctions
                                                   ? make_queue_lifecycle_probe_functions()
                                                   : default_d3d12_queue_native_functions();
    Result<D3d12QueueState> queueStateResult = create_d3d12_queue_state(
        device.Get(), a_descriptor.gpuWaitTimeoutMilliseconds, a_assertContext, queueFunctions);

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
