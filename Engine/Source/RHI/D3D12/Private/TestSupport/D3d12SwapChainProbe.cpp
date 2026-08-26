#include <Cue/RHI/D3D12/TestSupport/D3d12SwapChainProbe.h>

#include "D3d12AdapterSelection.h"
#include "D3d12DeviceCreation.h"
#include "D3d12Diagnostics.h"
#include "D3d12ProbeUtilities.h"
#include "D3d12QueueState.h"
#include "D3d12SwapChainState.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>

#include <array>
#include <iterator>
#include <memory>
#include <utility>

using cue::d3d12_test_private::count_info_queue_errors;

namespace
{
enum class ProbeFault
{
    None,
    TearingQuery,
    SwapChainCreation,
    AltEnter,
    Interface,
    FirstBackBuffer,
    SecondBackBuffer,
    BackBufferName,
    InvalidCurrentIndex,
    Resize,
    ReacquireBackBuffer,
};

enum class ProbeTearingOverride
{
    Native,
    Supported,
    Unsupported,
};

enum class ProbePresentOverride
{
    Native,
    Presented,
    Occluded,
};

struct ProbeNativeState final
{
    ProbeFault fault = ProbeFault::None;
    ProbeTearingOverride tearingOverride = ProbeTearingOverride::Native;
    ProbePresentOverride presentOverride = ProbePresentOverride::Native;
    DXGI_SWAP_CHAIN_DESC1 descriptor = {};
    bool descriptorCaptured = false;
    bool altEnterDisabled = false;
    bool failureHandlerCalled = false;
    bool failureResourcesWereAlive = false;
    bool presentCaptured = false;
    UINT presentSyncInterval = 0;
    UINT presentFlags = 0;
};

struct ProbeObjects final
{
    /// @brief D3D12 Swap Chain Probe に必要な Native Object と State の所有権を束ねる
    ProbeObjects(Microsoft::WRL::ComPtr<IDXGIFactory6> a_factory, Microsoft::WRL::ComPtr<ID3D12Device> a_device,
                 cue::D3d12QueueState &&a_queueState, cue::D3d12DiagnosticsStatus a_diagnostics) noexcept
        : factory(std::move(a_factory)), device(std::move(a_device)), queueState(std::move(a_queueState)),
          diagnostics(a_diagnostics)
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

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    cue::D3d12QueueState queueState;
    cue::D3d12DiagnosticsStatus diagnostics;
};

thread_local ProbeNativeState g_probeState;

/// @brief Probe 間で状態が混ざらないよう Fault Injection 用 Global 状態を初期化する
void reset_probe_state(ProbeFault a_fault, ProbeTearingOverride a_tearingOverride = ProbeTearingOverride::Native,
                       ProbePresentOverride a_presentOverride = ProbePresentOverride::Native) noexcept
{
    g_probeState = {};
    g_probeState.fault = a_fault;
    g_probeState.tearingOverride = a_tearingOverride;
    g_probeState.presentOverride = a_presentOverride;
}

/// @brief Tearing Query 失敗または対応可否を注入し、未指定時は Native Feature Query へ転送する
HRESULT check_tearing_for_probe(IDXGIFactory6 *a_factory, BOOL *a_isSupported) noexcept
{
    if (g_probeState.fault == ProbeFault::TearingQuery)
    {
        return E_FAIL;
    }

    if (g_probeState.tearingOverride == ProbeTearingOverride::Supported)
    {
        *a_isSupported = TRUE;
        return S_OK;
    }

    if (g_probeState.tearingOverride == ProbeTearingOverride::Unsupported)
    {
        *a_isSupported = FALSE;
        return S_OK;
    }

    return a_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, a_isSupported, sizeof(*a_isSupported));
}

/// @brief Swap Chain Descriptor を記録し、指定時は生成失敗を注入して Rollback 経路を再現する
HRESULT create_swap_chain_for_probe(IDXGIFactory6 *a_factory, ID3D12CommandQueue *a_queue, HWND a_window,
                                    const DXGI_SWAP_CHAIN_DESC1 *a_descriptor, IDXGISwapChain1 **a_swapChain) noexcept
{
    g_probeState.descriptor = *a_descriptor;
    g_probeState.descriptorCaptured = true;

    if (g_probeState.fault == ProbeFault::SwapChainCreation)
    {
        return E_FAIL;
    }

    return a_factory->CreateSwapChainForHwnd(a_queue, a_window, a_descriptor, nullptr, nullptr, a_swapChain);
}

/// @brief Alt+Enter 無効化失敗を指定時に注入し、通常時は Native 設定結果を記録する
HRESULT disable_alt_enter_for_probe(IDXGIFactory6 *a_factory, HWND a_window) noexcept
{
    if (g_probeState.fault == ProbeFault::AltEnter)
    {
        return E_FAIL;
    }

    const HRESULT result = a_factory->MakeWindowAssociation(a_window, DXGI_MWA_NO_ALT_ENTER);
    g_probeState.altEnterDisabled = SUCCEEDED(result);
    return result;
}

/// @brief Swap Chain 3 Interface 取得失敗を指定時に注入し、通常時は Native QueryInterface へ転送する
HRESULT query_swap_chain_for_probe(IDXGISwapChain1 *a_swapChain, IDXGISwapChain3 **a_swapChain3) noexcept
{
    if (g_probeState.fault == ProbeFault::Interface)
    {
        return E_NOINTERFACE;
    }

    return a_swapChain->QueryInterface(IID_PPV_ARGS(a_swapChain3));
}

/// @brief 指定 Buffer または再取得時の GetBuffer 失敗を注入し、通常時は Native Back Buffer を返す
HRESULT get_back_buffer_for_probe(IDXGISwapChain3 *a_swapChain, std::uint32_t a_index,
                                  ID3D12Resource **a_backBuffer) noexcept
{
    if ((g_probeState.fault == ProbeFault::FirstBackBuffer && a_index == 0) ||
        (g_probeState.fault == ProbeFault::SecondBackBuffer && a_index == 1) ||
        (g_probeState.fault == ProbeFault::ReacquireBackBuffer && a_index == 0))
    {
        return E_FAIL;
    }

    return a_swapChain->GetBuffer(a_index, IID_PPV_ARGS(a_backBuffer));
}

/// @brief InvalidCurrentIndex 時は範囲外 Index を注入し、通常時は Native Back Buffer Index を返す
std::uint32_t get_current_index_for_probe(IDXGISwapChain3 *a_swapChain) noexcept
{
    if (g_probeState.fault == ProbeFault::InvalidCurrentIndex)
    {
        return cue::k_d3d12SwapChainBufferCount;
    }

    return a_swapChain->GetCurrentBackBufferIndex();
}

/// @brief Back Buffer Name 設定失敗を指定時に注入し、通常時は Native SetName へ転送する
HRESULT set_name_for_probe(ID3D12Object *a_object, LPCWSTR a_name) noexcept
{
    if (g_probeState.fault == ProbeFault::BackBufferName)
    {
        return E_FAIL;
    }

    return a_object->SetName(a_name);
}

/// @brief Resize 失敗を指定時に注入し、通常時は Native ResizeBuffers へ転送する
HRESULT resize_buffers_for_probe(IDXGISwapChain3 *a_swapChain, std::uint32_t a_bufferCount, std::uint32_t a_width,
                                 std::uint32_t a_height, DXGI_FORMAT a_format, std::uint32_t a_flags) noexcept
{
    if (g_probeState.fault == ProbeFault::Resize)
    {
        return E_FAIL;
    }

    return a_swapChain->ResizeBuffers(a_bufferCount, a_width, a_height, a_format, a_flags);
}

/// @brief Present 引数を記録し、成功または Occluded 結果を注入して通常時は Native Present へ転送する
HRESULT present_for_probe(IDXGISwapChain3 *a_swapChain, UINT a_syncInterval, UINT a_flags) noexcept
{
    g_probeState.presentCaptured = true;
    g_probeState.presentSyncInterval = a_syncInterval;
    g_probeState.presentFlags = a_flags;

    if (g_probeState.presentOverride == ProbePresentOverride::Presented)
    {
        return S_OK;
    }

    if (g_probeState.presentOverride == ProbePresentOverride::Occluded)
    {
        return DXGI_STATUS_OCCLUDED;
    }

    return a_swapChain->Present(a_syncInterval, a_flags);
}

/// @brief Swap Chain の全 Native 境界を Fault Injection Callback へ差し替える関数 Table を返す
[[nodiscard]] cue::D3d12SwapChainNativeFunctions make_probe_functions() noexcept
{
    return {
        check_tearing_for_probe,   create_swap_chain_for_probe, disable_alt_enter_for_probe, query_swap_chain_for_probe,
        get_back_buffer_for_probe, get_current_index_for_probe, set_name_for_probe,        resize_buffers_for_probe,
        present_for_probe,
    };
}

/// @brief D3D12 Swap Chain Probe の Native Failure For Probe を規定された順序と失敗規則で処理する
cue::Result<void> handle_native_failure_for_probe(void *, cue::Error &&a_error,
                                                  const cue::D3d12SwapChainFailureResources &a_resources) noexcept
{
    const bool hasBaseSwapChain = a_resources.baseSwapChain != nullptr;
    const bool hasSwapChain = a_resources.swapChain != nullptr;
    const bool hasFirstBackBuffer = a_resources.backBuffers[0] != nullptr;
    const bool hasSecondBackBuffer = a_resources.backBuffers[1] != nullptr;
    bool resourcesAreValid = false;

    switch (g_probeState.fault)
    {
    case ProbeFault::TearingQuery:
    case ProbeFault::SwapChainCreation:
        resourcesAreValid = !hasBaseSwapChain && !hasSwapChain && !hasFirstBackBuffer && !hasSecondBackBuffer;
        break;
    case ProbeFault::AltEnter:
    case ProbeFault::Interface:
        resourcesAreValid = hasBaseSwapChain && !hasSwapChain && !hasFirstBackBuffer && !hasSecondBackBuffer;
        break;
    case ProbeFault::FirstBackBuffer:
        resourcesAreValid = hasBaseSwapChain && hasSwapChain && !hasFirstBackBuffer && !hasSecondBackBuffer;
        break;
    case ProbeFault::SecondBackBuffer:
        resourcesAreValid = hasBaseSwapChain && hasSwapChain && hasFirstBackBuffer && !hasSecondBackBuffer;
        break;
    case ProbeFault::BackBufferName:
        resourcesAreValid = hasBaseSwapChain && hasSwapChain && hasFirstBackBuffer && !hasSecondBackBuffer;
        break;
    case ProbeFault::Resize:
    case ProbeFault::ReacquireBackBuffer:
        resourcesAreValid = !hasBaseSwapChain && hasSwapChain && !hasFirstBackBuffer && !hasSecondBackBuffer;
        break;
    default:
        resourcesAreValid = false;
        break;
    }

    g_probeState.failureHandlerCalled = true;
    g_probeState.failureResourcesWereAlive = resourcesAreValid;
    return cue::Result<void>::failure(std::move(a_error));
}

/// @brief D3D12 Swap Chain Probe で使用する Probe Objects を生成し、呼び出し元へ返す
[[nodiscard]] cue::Result<std::unique_ptr<ProbeObjects>> create_probe_objects(
    bool a_enableDiagnostics, const cue::AssertContext &a_assertContext) noexcept
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

    cue::Result<cue::D3d12QueueState> queueResult =
        cue::create_d3d12_queue_state(device.Get(), descriptor.gpuWaitTimeoutMilliseconds, a_assertContext);

    if (!queueResult)
    {
        return cue::Result<std::unique_ptr<ProbeObjects>>::failure(std::move(*queueResult.try_error()));
    }

    cue::D3d12QueueState queueState = std::move(*queueResult.try_value());
    std::unique_ptr<ProbeObjects> objects = std::make_unique<ProbeObjects>(
        std::move(selection.factory), std::move(device), std::move(queueState), diagnostics);
    return cue::Result<std::unique_ptr<ProbeObjects>>::success(std::move(objects));
}

/// @brief Error が指定された Domain と Code を保持しているかを判定する
[[nodiscard]] bool has_error_code(const cue::Error *a_error, std::int64_t a_value) noexcept
{
    return a_error != nullptr && a_error->code().domain() == "Cue.RHI.D3D12" && a_error->code().value() == a_value;
}

/// @brief D3D12 Swap Chain Probe の Native Error Domain 条件を判定して返す
[[nodiscard]] bool has_native_error_domain(const cue::Error *a_error, std::string_view a_domain) noexcept
{
    if (a_error == nullptr)
    {
        return false;
    }

    const cue::NativeError *nativeError = a_error->try_native_error();
    return nativeError != nullptr && nativeError->domain() == a_domain;
}

/// @brief D3D12 Swap Chain Probe で使用する Descriptor を生成し、呼び出し元へ返す
[[nodiscard]] cue::D3d12SwapChainDescriptor make_descriptor(const void *a_nativeWindow, std::uint32_t a_width,
                                                            std::uint32_t a_height, bool a_isVsyncEnabled) noexcept
{
    return {
        static_cast<HWND>(const_cast<void *>(a_nativeWindow)),
        a_width,
        a_height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        a_isVsyncEnabled,
    };
}

/// @brief D3D12 Swap Chain Probe の Probe Objects を依存関係と完了条件を守って安全に解放または停止する
[[nodiscard]] bool shutdown_probe_objects(std::unique_ptr<ProbeObjects> &a_objects) noexcept
{
    cue::Result<void> queueResult = a_objects->queueState.shutdown();
    return static_cast<bool>(queueResult);
}
} // namespace

namespace cue
{
Result<D3d12SwapChainProbeReport> probe_d3d12_swap_chain(const void *a_nativeWindow, std::uint32_t a_width,
                                                         std::uint32_t a_height, bool a_isVsyncEnabled,
                                                         const AssertContext &a_assertContext) noexcept
{
    reset_probe_state(ProbeFault::None);
    Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(true, a_assertContext);

    if (!objectsResult)
    {
        return Result<D3d12SwapChainProbeReport>::failure(std::move(*objectsResult.try_error()));
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    D3d12SwapChainDescriptor descriptor = make_descriptor(a_nativeWindow, a_width, a_height, a_isVsyncEnabled);
    D3d12SwapChainNativeFunctions functions = make_probe_functions();
    Result<D3d12SwapChainState> stateResult =
        create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                      descriptor, a_assertContext, functions);

    if (!stateResult)
    {
        static_cast<void>(shutdown_probe_objects(objects));
        return Result<D3d12SwapChainProbeReport>::failure(std::move(*stateResult.try_error()));
    }

    D3d12SwapChainState state = std::move(*stateResult.try_value());
    Result<ID3D12Resource *> firstBuffer = state.back_buffer(0);
    Result<ID3D12Resource *> secondBuffer = state.back_buffer(1);
    const bool buffersAvailable = firstBuffer && secondBuffer && *firstBuffer.try_value() != nullptr &&
                                  *secondBuffer.try_value() != nullptr &&
                                  *firstBuffer.try_value() != *secondBuffer.try_value();
    const bool descriptorValid =
        g_probeState.descriptorCaptured && g_probeState.descriptor.Width == a_width &&
        g_probeState.descriptor.Height == a_height && g_probeState.descriptor.Format == DXGI_FORMAT_R8G8B8A8_UNORM &&
        g_probeState.descriptor.SampleDesc.Count == 1 && g_probeState.descriptor.SampleDesc.Quality == 0 &&
        g_probeState.descriptor.BufferUsage == DXGI_USAGE_RENDER_TARGET_OUTPUT &&
        g_probeState.descriptor.BufferCount == k_d3d12SwapChainBufferCount &&
        g_probeState.descriptor.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3d12SwapChainProbeReport report = {};
    report.width = state.width();
    report.height = state.height();
    report.bufferCount = state.buffer_count();
    report.currentBackBufferIndex = state.current_back_buffer_index();
    report.descriptorShapeIsValid = descriptorValid;
    report.backBuffersAreAvailable = buffersAvailable;
    report.altEnterWasDisabled = g_probeState.altEnterDisabled;
    report.isVsyncEnabled = state.is_vsync_enabled();
    report.isTearingSupported = state.is_tearing_supported();
    report.isTearingEnabled = state.is_tearing_enabled();
    report.diagnosticsAvailable = objects->diagnostics.isInfoQueueEnabled;
    Result<void> stateShutdownResult = state.shutdown();
    Result<void> queueShutdownResult = objects->queueState.shutdown();

    if (!stateShutdownResult || !queueShutdownResult)
    {
        Error error = !stateShutdownResult ? std::move(*stateShutdownResult.try_error())
                                           : std::move(*queueShutdownResult.try_error());
        return Result<D3d12SwapChainProbeReport>::failure(std::move(error));
    }

    Result<void> liveObjectResult =
        report_d3d12_live_device_objects(objects->device.Get(), objects->diagnostics, a_assertContext);

    if (!liveObjectResult)
    {
        return Result<D3d12SwapChainProbeReport>::failure(std::move(*liveObjectResult.try_error()));
    }

    report.infoQueueErrorCount = count_info_queue_errors(objects->device.Get());
    return Result<D3d12SwapChainProbeReport>::success(std::move(report));
}

bool verify_d3d12_swap_chain_tearing_matrix_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                      std::uint32_t a_height,
                                                      const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    struct Case final
    {
        bool isVsyncEnabled;
        ProbeTearingOverride tearingOverride;
        bool expectedTearingEnabled;
    };
    constexpr Case cases[] = {
        {true, ProbeTearingOverride::Supported, false},
        {false, ProbeTearingOverride::Supported, true},
        {false, ProbeTearingOverride::Unsupported, false},
    };
    bool valid = true;

    for (const Case &testCase : cases)
    {
        reset_probe_state(ProbeFault::None, testCase.tearingOverride);
        D3d12SwapChainDescriptor descriptor =
            make_descriptor(a_nativeWindow, a_width, a_height, testCase.isVsyncEnabled);
        D3d12SwapChainNativeFunctions functions = make_probe_functions();
        Result<D3d12SwapChainState> stateResult =
            create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                          descriptor, a_assertContext, functions);

        if (!stateResult)
        {
            valid = false;
            break;
        }

        D3d12SwapChainState state = std::move(*stateResult.try_value());
        const bool flagEnabled = (g_probeState.descriptor.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
        valid = valid && state.is_tearing_enabled() == testCase.expectedTearingEnabled &&
                flagEnabled == testCase.expectedTearingEnabled &&
                !(state.is_vsync_enabled() && state.is_tearing_enabled()) && state.shutdown();
    }

    return shutdown_probe_objects(objects) && valid;
}

bool verify_d3d12_swap_chain_present_matrix_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                      std::uint32_t a_height,
                                                      const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    struct Case final
    {
        bool isVsyncEnabled;
        ProbeTearingOverride tearingOverride;
        ProbePresentOverride presentOverride;
        UINT expectedSyncInterval;
        UINT expectedFlags;
        bool expectOccluded;
    };
    constexpr Case cases[] = {
        {true, ProbeTearingOverride::Supported, ProbePresentOverride::Presented, 1, 0, false},
        {false, ProbeTearingOverride::Supported, ProbePresentOverride::Presented, 0,
         DXGI_PRESENT_ALLOW_TEARING, false},
        {false, ProbeTearingOverride::Unsupported, ProbePresentOverride::Presented, 0, 0, false},
        {true, ProbeTearingOverride::Supported, ProbePresentOverride::Occluded, 1, 0, true},
    };
    bool valid = true;

    for (const Case &testCase : cases)
    {
        reset_probe_state(ProbeFault::None, testCase.tearingOverride, testCase.presentOverride);
        D3d12SwapChainDescriptor descriptor =
            make_descriptor(a_nativeWindow, a_width, a_height, testCase.isVsyncEnabled);
        D3d12SwapChainNativeFunctions functions = make_probe_functions();
        Result<D3d12SwapChainState> stateResult =
            create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                          descriptor, a_assertContext, functions);

        if (!stateResult)
        {
            valid = false;
            break;
        }

        D3d12SwapChainState state = std::move(*stateResult.try_value());
        Result<D3d12PresentStatus> presentResult = state.present();
        const D3d12PresentStatus expectedStatus =
            testCase.expectOccluded ? D3d12PresentStatus::Occluded : D3d12PresentStatus::Presented;
        const bool statusValid = presentResult && *presentResult.try_value() == expectedStatus;
        valid = valid && statusValid && g_probeState.presentCaptured &&
                g_probeState.presentSyncInterval == testCase.expectedSyncInterval &&
                g_probeState.presentFlags == testCase.expectedFlags && state.shutdown();
    }

    return shutdown_probe_objects(objects) && valid;
}

bool verify_d3d12_swap_chain_faults_for_probe(const void *a_nativeWindow, std::uint32_t a_width, std::uint32_t a_height,
                                              const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(true, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    constexpr ProbeFault faults[] = {
        ProbeFault::TearingQuery,    ProbeFault::SwapChainCreation, ProbeFault::AltEnter,       ProbeFault::Interface,
        ProbeFault::FirstBackBuffer, ProbeFault::SecondBackBuffer,  ProbeFault::BackBufferName,
    };
    constexpr std::int64_t expectedCodes[] = {78, 79, 80, 81, 82, 82, 83};
    constexpr std::string_view expectedNativeDomains[] = {"DXGI", "DXGI", "DXGI", "DXGI", "DXGI", "DXGI", "D3D12"};
    bool valid = true;

    for (std::size_t index = 0; index < std::size(faults); ++index)
    {
        reset_probe_state(faults[index]);
        D3d12SwapChainDescriptor descriptor = make_descriptor(a_nativeWindow, a_width, a_height, true);
        D3d12SwapChainNativeFunctions functions = make_probe_functions();
        D3d12SwapChainFailureHandler failureHandler = {
            nullptr,
            handle_native_failure_for_probe,
        };
        Result<D3d12SwapChainState> result =
            create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                          descriptor, a_assertContext, functions, failureHandler);
        valid = valid && has_error_code(result.try_error(), expectedCodes[index]) &&
                has_native_error_domain(result.try_error(), expectedNativeDomains[index]) &&
                g_probeState.failureHandlerCalled && g_probeState.failureResourcesWereAlive;

        if (result)
        {
            static_cast<void>(result.try_value()->shutdown());
            valid = false;
        }
    }

    reset_probe_state(ProbeFault::None);
    D3d12SwapChainDescriptor descriptor = make_descriptor(a_nativeWindow, a_width, a_height, true);
    D3d12SwapChainNativeFunctions functions = make_probe_functions();
    Result<D3d12SwapChainState> recoveryResult =
        create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                      descriptor, a_assertContext, functions);

    if (!recoveryResult)
    {
        valid = false;
    }
    else
    {
        valid = static_cast<bool>(recoveryResult.try_value()->shutdown()) && valid;
    }

    const bool shutdownSucceeded = shutdown_probe_objects(objects);
    const bool diagnosticsAllowed = are_d3d12_diagnostics_allowed();
    Result<void> liveObjectResult =
        report_d3d12_live_device_objects(objects->device.Get(), objects->diagnostics, a_assertContext);
    const bool diagnosticsValid = !diagnosticsAllowed || (objects->diagnostics.isInfoQueueEnabled && liveObjectResult &&
                                                          count_info_queue_errors(objects->device.Get()) == 0);
    return shutdownSucceeded && diagnosticsValid && valid;
}

bool verify_d3d12_swap_chain_log_failure_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                   std::uint32_t a_height, const AssertContext &a_setupAssertContext,
                                                   const AssertContext &a_failingAssertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(false, a_setupAssertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    reset_probe_state(ProbeFault::None);
    D3d12SwapChainDescriptor descriptor = make_descriptor(a_nativeWindow, a_width, a_height, true);
    D3d12SwapChainNativeFunctions functions = make_probe_functions();
    Result<D3d12SwapChainState> failureResult =
        create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                      descriptor, a_failingAssertContext, functions);
    const bool failureIsValid = has_error_code(failureResult.try_error(), 85);

    if (failureResult)
    {
        static_cast<void>(failureResult.try_value()->shutdown());
        static_cast<void>(shutdown_probe_objects(objects));
        return false;
    }

    reset_probe_state(ProbeFault::None);
    Result<D3d12SwapChainState> recoveryResult =
        create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                      descriptor, a_setupAssertContext, functions);

    if (!recoveryResult)
    {
        static_cast<void>(shutdown_probe_objects(objects));
        return false;
    }

    Result<void> stateShutdownResult = recoveryResult.try_value()->shutdown();
    return failureIsValid && stateShutdownResult && shutdown_probe_objects(objects);
}

bool verify_d3d12_swap_chain_validation_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                  std::uint32_t a_height, const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    D3d12SwapChainNativeFunctions functions = make_probe_functions();
    D3d12SwapChainDescriptor invalidDescriptor = make_descriptor(nullptr, a_width, a_height, true);
    Result<D3d12SwapChainState> invalidDescriptorResult =
        create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                      invalidDescriptor, a_assertContext, functions);
    reset_probe_state(ProbeFault::InvalidCurrentIndex);
    D3d12SwapChainDescriptor descriptor = make_descriptor(a_nativeWindow, a_width, a_height, true);
    Result<D3d12SwapChainState> invalidIndexResult =
        create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                      descriptor, a_assertContext, functions);
    const bool valid =
        has_error_code(invalidDescriptorResult.try_error(), 77) && has_error_code(invalidIndexResult.try_error(), 84);
    return shutdown_probe_objects(objects) && valid;
}

bool verify_d3d12_swap_chain_resize_failure_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                       std::uint32_t a_height,
                                                       const AssertContext &a_assertContext) noexcept
{
    Result<std::unique_ptr<ProbeObjects>> objectsResult = create_probe_objects(false, a_assertContext);

    if (!objectsResult)
    {
        return false;
    }

    std::unique_ptr<ProbeObjects> objects = std::move(*objectsResult.try_value());
    D3d12SwapChainDescriptor descriptor = make_descriptor(a_nativeWindow, a_width, a_height, true);
    D3d12SwapChainNativeFunctions functions = make_probe_functions();
    D3d12SwapChainFailureHandler failureHandler = {nullptr, handle_native_failure_for_probe};
    constexpr std::array<ProbeFault, 2> resizeFaults = {
        ProbeFault::Resize,
        ProbeFault::ReacquireBackBuffer,
    };

    for (ProbeFault fault : resizeFaults)
    {
        reset_probe_state(ProbeFault::None);
        Result<D3d12SwapChainState> stateResult =
            create_d3d12_swap_chain_state(objects->factory.Get(), objects->queueState.native_queue_for_presentation(),
                                          descriptor, a_assertContext, functions, failureHandler);

        if (!stateResult)
        {
            static_cast<void>(shutdown_probe_objects(objects));
            return false;
        }

        D3d12SwapChainState state = std::move(*stateResult.try_value());
        reset_probe_state(fault);
        Result<void> resizeResult = state.resize(a_width + 1, a_height + 1);
        const std::int64_t expectedCode = fault == ProbeFault::Resize ? 90 : 82;
        const std::uint32_t expectedWidth = fault == ProbeFault::Resize ? a_width : a_width + 1;
        const std::uint32_t expectedHeight = fault == ProbeFault::Resize ? a_height : a_height + 1;
        const bool valid = !resizeResult && has_error_code(resizeResult.try_error(), expectedCode) &&
                           has_native_error_domain(resizeResult.try_error(), "DXGI") &&
                           g_probeState.failureHandlerCalled && g_probeState.failureResourcesWereAlive &&
                           !state.has_all_back_buffers() && state.width() == expectedWidth &&
                           state.height() == expectedHeight;
        Result<void> stateShutdownResult = state.shutdown();

        if (!valid || !stateShutdownResult)
        {
            static_cast<void>(shutdown_probe_objects(objects));
            return false;
        }
    }

    return shutdown_probe_objects(objects);
}
} // namespace cue
