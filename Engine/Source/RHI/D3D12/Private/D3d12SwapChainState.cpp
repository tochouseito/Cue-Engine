#include "D3d12SwapChainState.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Log.h>

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::int64_t k_invalidDescriptor = 77;
constexpr std::int64_t k_tearingQueryFailed = 78;
constexpr std::int64_t k_swapChainCreationFailed = 79;
constexpr std::int64_t k_altEnterPolicyFailed = 80;
constexpr std::int64_t k_swapChainInterfaceFailed = 81;
constexpr std::int64_t k_backBufferAcquisitionFailed = 82;
constexpr std::int64_t k_backBufferNameFailed = 83;
constexpr std::int64_t k_invalidBackBufferIndex = 84;
constexpr std::int64_t k_swapChainLogFailed = 85;
constexpr std::int64_t k_swapChainShutdown = 86;
constexpr std::int64_t k_swapChainResizeFailed = 90;
constexpr std::int64_t k_swapChainPresentFailed = 98;

[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("D3D12 Swap Chain diagnostic allocation failed");
    std::abort();
}

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
                                           HRESULT a_nativeCode) noexcept
{
    cue::NativeError nativeError =
        cue::NativeError::create(a_context.fatal_handler(), a_nativeDomain, static_cast<std::int64_t>(a_nativeCode));
    return cue::Error::create(a_context.fatal_handler(), make_code(a_context, a_code), a_summary,
                              std::move(nativeError));
}

[[nodiscard]] cue::D3d12SwapChainFailureResources make_failure_resources(
    IDXGISwapChain1 *a_baseSwapChain, IDXGISwapChain3 *a_swapChain,
    const std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, cue::k_d3d12SwapChainBufferCount> &a_backBuffers) noexcept
{
    cue::D3d12SwapChainFailureResources resources = {};
    resources.baseSwapChain = a_baseSwapChain;
    resources.swapChain = a_swapChain;

    for (std::uint32_t index = 0; index < cue::k_d3d12SwapChainBufferCount; ++index)
    {
        resources.backBuffers[index] = a_backBuffers[index].Get();
    }

    return resources;
}

[[nodiscard]] cue::Result<cue::D3d12SwapChainState> fail_native_creation(
    cue::Error &&a_error, const cue::D3d12SwapChainFailureHandler &a_failureHandler,
    const cue::D3d12SwapChainFailureResources &a_resources, const cue::AssertContext &a_assertContext) noexcept
{
    if (a_failureHandler.handleNativeFailure == nullptr)
    {
        return cue::Result<cue::D3d12SwapChainState>::failure(std::move(a_error));
    }

    cue::Result<void> handlerResult =
        a_failureHandler.handleNativeFailure(a_failureHandler.owner, std::move(a_error), a_resources);

    if (handlerResult)
    {
        a_assertContext.fatal_handler().terminate("D3D12 native failure handler did not retain an Error");
    }

    return cue::Result<cue::D3d12SwapChainState>::failure(std::move(*handlerResult.try_error()));
}

HRESULT check_tearing_support(IDXGIFactory6 *a_factory, BOOL *a_isSupported) noexcept
{
    return a_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, a_isSupported, sizeof(*a_isSupported));
}

HRESULT create_swap_chain_for_window(IDXGIFactory6 *a_factory, ID3D12CommandQueue *a_queue, HWND a_window,
                                     const DXGI_SWAP_CHAIN_DESC1 *a_descriptor, IDXGISwapChain1 **a_swapChain) noexcept
{
    return a_factory->CreateSwapChainForHwnd(a_queue, a_window, a_descriptor, nullptr, nullptr, a_swapChain);
}

HRESULT disable_alt_enter(IDXGIFactory6 *a_factory, HWND a_window) noexcept
{
    return a_factory->MakeWindowAssociation(a_window, DXGI_MWA_NO_ALT_ENTER);
}

HRESULT query_swap_chain_3(IDXGISwapChain1 *a_swapChain, IDXGISwapChain3 **a_swapChain3) noexcept
{
    return a_swapChain->QueryInterface(IID_PPV_ARGS(a_swapChain3));
}

HRESULT get_back_buffer(IDXGISwapChain3 *a_swapChain, std::uint32_t a_index, ID3D12Resource **a_backBuffer) noexcept
{
    return a_swapChain->GetBuffer(a_index, IID_PPV_ARGS(a_backBuffer));
}

std::uint32_t get_current_back_buffer_index(IDXGISwapChain3 *a_swapChain) noexcept
{
    return a_swapChain->GetCurrentBackBufferIndex();
}

HRESULT set_object_name(ID3D12Object *a_object, LPCWSTR a_name) noexcept
{
    return a_object->SetName(a_name);
}

HRESULT resize_buffers(IDXGISwapChain3 *a_swapChain, std::uint32_t a_bufferCount, std::uint32_t a_width,
                       std::uint32_t a_height, DXGI_FORMAT a_format, std::uint32_t a_flags) noexcept
{
    return a_swapChain->ResizeBuffers(a_bufferCount, a_width, a_height, a_format, a_flags);
}

HRESULT present_swap_chain(IDXGISwapChain3 *a_swapChain, UINT a_syncInterval, UINT a_flags) noexcept
{
    return a_swapChain->Present(a_syncInterval, a_flags);
}

[[nodiscard]] cue::Result<void> log_swap_chain(const cue::D3d12SwapChainDescriptor &a_descriptor,
                                               std::uint32_t a_currentBackBufferIndex, bool a_isTearingSupported,
                                               bool a_isTearingEnabled,
                                               const cue::AssertContext &a_assertContext) noexcept
{
    try
    {
        std::string message = "D3D12 Swap Chain ready: Width=" + std::to_string(a_descriptor.width) +
                              ", Height=" + std::to_string(a_descriptor.height) +
                              ", Format=" + std::to_string(static_cast<std::uint32_t>(a_descriptor.format)) +
                              ", BufferCount=" + std::to_string(cue::k_d3d12SwapChainBufferCount) +
                              ", CurrentBackBufferIndex=" + std::to_string(a_currentBackBufferIndex) +
                              ", VSync=" + (a_descriptor.isVsyncEnabled ? "true" : "false") +
                              ", TearingSupported=" + (a_isTearingSupported ? "true" : "false") +
                              ", TearingEnabled=" + (a_isTearingEnabled ? "true" : "false");
        const cue::LogResult result = a_assertContext.logger().log(cue::LogLevel::Info, message);

        if (result != cue::LogResult::Success)
        {
            return cue::Result<void>::failure(
                make_error(a_assertContext, k_swapChainLogFailed, "D3D12 Swap Chain diagnostics could not be logged"));
        }

        return cue::Result<void>::success();
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
}
} // namespace

namespace cue
{
const D3d12SwapChainNativeFunctions &default_d3d12_swap_chain_native_functions() noexcept
{
    static const D3d12SwapChainNativeFunctions functions = {
        check_tearing_support, create_swap_chain_for_window,  disable_alt_enter, query_swap_chain_3,
        get_back_buffer,       get_current_back_buffer_index, set_object_name,   resize_buffers,
        present_swap_chain,
    };
    return functions;
}

D3d12SwapChainState::D3d12SwapChainState(
    Microsoft::WRL::ComPtr<IDXGISwapChain3> a_swapChain,
    D3d12SwapChainBackBuffers &&a_backBuffers,
    const D3d12SwapChainDescriptor &a_descriptor, std::uint32_t a_currentBackBufferIndex, bool a_isTearingSupported,
    bool a_isTearingEnabled, const D3d12SwapChainNativeFunctions &a_functions,
    const D3d12SwapChainFailureHandler &a_failureHandler,
    const AssertContext &a_assertContext) noexcept
    : m_swapChain(std::move(a_swapChain)), m_backBuffers(std::move(a_backBuffers)), m_functions(a_functions),
      m_failureHandler(a_failureHandler), m_assertContext(&a_assertContext), m_width(a_descriptor.width),
      m_height(a_descriptor.height),
      m_currentBackBufferIndex(a_currentBackBufferIndex), m_format(a_descriptor.format),
      m_isVsyncEnabled(a_descriptor.isVsyncEnabled), m_isTearingSupported(a_isTearingSupported),
      m_isTearingEnabled(a_isTearingEnabled)
{
}

D3d12SwapChainState::D3d12SwapChainState(D3d12SwapChainState &&a_other) noexcept
    : m_functions({}), m_failureHandler({}), m_assertContext(nullptr), m_width(0), m_height(0),
      m_currentBackBufferIndex(0),
      m_format(DXGI_FORMAT_UNKNOWN), m_isVsyncEnabled(false), m_isTearingSupported(false), m_isTearingEnabled(false)
{
    take_from(std::move(a_other));
}

D3d12SwapChainState &D3d12SwapChainState::operator=(D3d12SwapChainState &&a_other) noexcept
{
    if (this != &a_other)
    {
        if (has_native_objects())
        {
            m_assertContext->fatal_handler().terminate("D3D12 Swap Chain move assignment requires an empty target");
        }

        take_from(std::move(a_other));
    }

    return *this;
}

D3d12SwapChainState::~D3d12SwapChainState() noexcept
{
    if (has_native_objects())
    {
        m_assertContext->fatal_handler().terminate("D3D12 Swap Chain owner was destroyed before shutdown");
    }
}

void D3d12SwapChainState::take_from(D3d12SwapChainState &&a_other) noexcept
{
    m_swapChain = std::move(a_other.m_swapChain);
    m_backBuffers = std::move(a_other.m_backBuffers);
    m_functions = a_other.m_functions;
    m_failureHandler = a_other.m_failureHandler;
    m_assertContext = a_other.m_assertContext;
    m_width = a_other.m_width;
    m_height = a_other.m_height;
    m_currentBackBufferIndex = a_other.m_currentBackBufferIndex;
    m_format = a_other.m_format;
    m_isVsyncEnabled = a_other.m_isVsyncEnabled;
    m_isTearingSupported = a_other.m_isTearingSupported;
    m_isTearingEnabled = a_other.m_isTearingEnabled;

    a_other.m_width = 0;
    a_other.m_height = 0;
    a_other.m_currentBackBufferIndex = 0;
    a_other.m_format = DXGI_FORMAT_UNKNOWN;
    a_other.m_isVsyncEnabled = false;
    a_other.m_isTearingSupported = false;
    a_other.m_isTearingEnabled = false;
}

void D3d12SwapChainState::release_back_buffers() noexcept
{
    for (Microsoft::WRL::ComPtr<ID3D12Resource> &backBuffer : m_backBuffers)
    {
        backBuffer.Reset();
    }
}

Result<void> D3d12SwapChainState::acquire_back_buffers() noexcept
{
    constexpr LPCWSTR backBufferNames[k_d3d12SwapChainBufferCount] = {
        L"CueEngine D3D12 Swap Chain Back Buffer 0",
        L"CueEngine D3D12 Swap Chain Back Buffer 1",
    };

    release_back_buffers();

    for (std::uint32_t index = 0; index < k_d3d12SwapChainBufferCount; ++index)
    {
        const HRESULT bufferResult =
            m_functions.getBackBuffer(m_swapChain.Get(), index, m_backBuffers[index].GetAddressOf());

        if (FAILED(bufferResult))
        {
            return Result<void>::failure(make_native_error(*m_assertContext, k_backBufferAcquisitionFailed,
                                                           "D3D12 Swap Chain Back Buffer acquisition failed", "DXGI",
                                                           bufferResult));
        }

        const HRESULT nameResult = m_functions.setObjectName(m_backBuffers[index].Get(), backBufferNames[index]);

        if (FAILED(nameResult))
        {
            return Result<void>::failure(make_native_error(*m_assertContext, k_backBufferNameFailed,
                                                           "D3D12 Back Buffer diagnostic name could not be set",
                                                           "D3D12", nameResult));
        }
    }

    const std::uint32_t currentIndex = m_functions.getCurrentBackBufferIndex(m_swapChain.Get());

    if (currentIndex >= k_d3d12SwapChainBufferCount)
    {
        return Result<void>::failure(
            make_error(*m_assertContext, k_invalidBackBufferIndex, "DXGI Current Back Buffer index is out of range"));
    }

    m_currentBackBufferIndex = currentIndex;
    return Result<void>::success();
}

Result<void> D3d12SwapChainState::classify_native_failure(Error &&a_error) noexcept
{
    if (m_failureHandler.handleNativeFailure == nullptr)
    {
        return Result<void>::failure(std::move(a_error));
    }

    Result<void> handlerResult = m_failureHandler.handleNativeFailure(
        m_failureHandler.owner, std::move(a_error),
        make_failure_resources(nullptr, m_swapChain.Get(), m_backBuffers));

    if (handlerResult)
    {
        m_assertContext->fatal_handler().terminate("D3D12 native failure handler did not retain an Error");
    }

    return Result<void>::failure(std::move(*handlerResult.try_error()));
}

Result<std::uint32_t> D3d12SwapChainState::refresh_current_back_buffer_index() noexcept
{
    if (m_swapChain == nullptr)
    {
        return Result<std::uint32_t>::failure(
            make_error(*m_assertContext, k_swapChainShutdown, "D3D12 Swap Chain is shutdown"));
    }

    const std::uint32_t index = m_functions.getCurrentBackBufferIndex(m_swapChain.Get());

    if (index >= k_d3d12SwapChainBufferCount)
    {
        return Result<std::uint32_t>::failure(
            make_error(*m_assertContext, k_invalidBackBufferIndex, "DXGI Current Back Buffer index is out of range"));
    }

    m_currentBackBufferIndex = index;
    return Result<std::uint32_t>::success(std::move(m_currentBackBufferIndex));
}

Result<ID3D12Resource *> D3d12SwapChainState::back_buffer(std::uint32_t a_index) const noexcept
{
    if (a_index >= k_d3d12SwapChainBufferCount)
    {
        CUE_ASSERT(*m_assertContext, false, "D3D12 Back Buffer index is out of range");
        return Result<ID3D12Resource *>::failure(
            make_error(*m_assertContext, k_invalidBackBufferIndex, "D3D12 Back Buffer index is out of range"));
    }

    if (m_backBuffers[a_index] == nullptr)
    {
        return Result<ID3D12Resource *>::failure(
            make_error(*m_assertContext, k_swapChainShutdown, "D3D12 Back Buffer is unavailable"));
    }

    ID3D12Resource *resource = m_backBuffers[a_index].Get();
    return Result<ID3D12Resource *>::success(std::move(resource));
}

Result<D3d12SwapChainBackBuffers> D3d12SwapChainState::take_back_buffers() noexcept
{
    if (!has_all_back_buffers())
    {
        return Result<D3d12SwapChainBackBuffers>::failure(
            make_error(*m_assertContext, k_swapChainShutdown, "D3D12 Back Buffer ownership is unavailable"));
    }

    D3d12SwapChainBackBuffers backBuffers = std::move(m_backBuffers);
    return Result<D3d12SwapChainBackBuffers>::success(std::move(backBuffers));
}

Result<void> D3d12SwapChainState::resize(std::uint32_t a_width, std::uint32_t a_height) noexcept
{
    if (m_swapChain == nullptr)
    {
        return Result<void>::failure(
            make_error(*m_assertContext, k_swapChainShutdown, "D3D12 Swap Chain is shutdown"));
    }

    if (a_width == 0 || a_height == 0)
    {
        return Result<void>::failure(
            make_error(*m_assertContext, k_invalidDescriptor, "D3D12 Swap Chain Resize Size is invalid"));
    }

    if (a_width == m_width && a_height == m_height)
    {
        return Result<void>::success();
    }

    release_back_buffers();
    const std::uint32_t flags = m_isTearingEnabled ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    const HRESULT resizeResult = m_functions.resizeBuffers(m_swapChain.Get(), k_d3d12SwapChainBufferCount, a_width,
                                                           a_height, m_format, flags);

    if (FAILED(resizeResult))
    {
        Error resizeError = make_native_error(*m_assertContext, k_swapChainResizeFailed,
                                              "DXGI Swap Chain Resize failed", "DXGI", resizeResult);
        return classify_native_failure(std::move(resizeError));
    }

    m_width = a_width;
    m_height = a_height;
    Result<void> acquisitionResult = acquire_back_buffers();

    if (!acquisitionResult)
    {
        return classify_native_failure(std::move(*acquisitionResult.try_error()));
    }

    D3d12SwapChainDescriptor descriptor = {nullptr, m_width, m_height, m_format, m_isVsyncEnabled};
    return log_swap_chain(descriptor, m_currentBackBufferIndex, m_isTearingSupported, m_isTearingEnabled,
                          *m_assertContext);
}

Result<D3d12PresentStatus> D3d12SwapChainState::present() noexcept
{
    if (m_swapChain == nullptr)
    {
        return Result<D3d12PresentStatus>::failure(
            make_error(*m_assertContext, k_swapChainShutdown, "D3D12 Swap Chain is shutdown"));
    }

    const UINT syncInterval = m_isVsyncEnabled ? 1 : 0;
    const UINT flags = m_isTearingEnabled ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT presentResult = m_functions.present(m_swapChain.Get(), syncInterval, flags);

    if (FAILED(presentResult))
    {
        Error presentError = make_native_error(*m_assertContext, k_swapChainPresentFailed,
                                               "DXGI Swap Chain Present failed", "DXGI", presentResult);
        Result<void> classificationResult = classify_native_failure(std::move(presentError));
        return Result<D3d12PresentStatus>::failure(std::move(*classificationResult.try_error()));
    }

    D3d12PresentStatus status =
        presentResult == DXGI_STATUS_OCCLUDED ? D3d12PresentStatus::Occluded : D3d12PresentStatus::Presented;
    return Result<D3d12PresentStatus>::success(std::move(status));
}

Result<void> D3d12SwapChainState::shutdown() noexcept
{
    release_back_buffers();
    m_swapChain.Reset();
    m_width = 0;
    m_height = 0;
    m_currentBackBufferIndex = 0;
    m_format = DXGI_FORMAT_UNKNOWN;
    m_isVsyncEnabled = false;
    m_isTearingSupported = false;
    m_isTearingEnabled = false;
    return Result<void>::success();
}

std::uint32_t D3d12SwapChainState::width() const noexcept
{
    return m_width;
}

std::uint32_t D3d12SwapChainState::height() const noexcept
{
    return m_height;
}

DXGI_FORMAT D3d12SwapChainState::format() const noexcept
{
    return m_format;
}

std::uint32_t D3d12SwapChainState::buffer_count() const noexcept
{
    return k_d3d12SwapChainBufferCount;
}

std::uint32_t D3d12SwapChainState::current_back_buffer_index() const noexcept
{
    return m_currentBackBufferIndex;
}

bool D3d12SwapChainState::is_vsync_enabled() const noexcept
{
    return m_isVsyncEnabled;
}

bool D3d12SwapChainState::is_tearing_supported() const noexcept
{
    return m_isTearingSupported;
}

bool D3d12SwapChainState::is_tearing_enabled() const noexcept
{
    return m_isTearingEnabled;
}

bool D3d12SwapChainState::has_all_back_buffers() const noexcept
{
    for (const Microsoft::WRL::ComPtr<ID3D12Resource> &backBuffer : m_backBuffers)
    {
        if (backBuffer == nullptr)
        {
            return false;
        }
    }

    return true;
}

bool D3d12SwapChainState::has_native_objects() const noexcept
{
    if (m_swapChain != nullptr)
    {
        return true;
    }

    for (const Microsoft::WRL::ComPtr<ID3D12Resource> &backBuffer : m_backBuffers)
    {
        if (backBuffer != nullptr)
        {
            return true;
        }
    }

    return false;
}

Result<D3d12SwapChainState> create_d3d12_swap_chain_state(IDXGIFactory6 *a_factory, ID3D12CommandQueue *a_queue,
                                                          const D3d12SwapChainDescriptor &a_descriptor,
                                                          const AssertContext &a_assertContext,
                                                          const D3d12SwapChainNativeFunctions &a_functions,
                                                          const D3d12SwapChainFailureHandler &a_failureHandler) noexcept
{
    if (a_factory == nullptr || a_queue == nullptr || a_descriptor.window == nullptr || a_descriptor.width == 0 ||
        a_descriptor.height == 0 || a_descriptor.format == DXGI_FORMAT_UNKNOWN)
    {
        return Result<D3d12SwapChainState>::failure(
            make_error(a_assertContext, k_invalidDescriptor, "D3D12 Swap Chain descriptor is invalid"));
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain1> baseSwapChain;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
    D3d12SwapChainBackBuffers backBuffers = {};
    BOOL tearingSupported = FALSE;
    const HRESULT tearingResult = a_functions.checkTearingSupport(a_factory, &tearingSupported);

    if (FAILED(tearingResult))
    {
        return fail_native_creation(make_native_error(a_assertContext, k_tearingQueryFailed,
                                                      "DXGI tearing support query failed", "DXGI", tearingResult),
                                    a_failureHandler,
                                    make_failure_resources(baseSwapChain.Get(), swapChain.Get(), backBuffers),
                                    a_assertContext);
    }

    const bool isTearingSupported = tearingSupported != FALSE;
    const bool isTearingEnabled = !a_descriptor.isVsyncEnabled && isTearingSupported;
    DXGI_SWAP_CHAIN_DESC1 nativeDescriptor = {};
    nativeDescriptor.Width = a_descriptor.width;
    nativeDescriptor.Height = a_descriptor.height;
    nativeDescriptor.Format = a_descriptor.format;
    nativeDescriptor.Stereo = FALSE;
    nativeDescriptor.SampleDesc = {1, 0};
    nativeDescriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    nativeDescriptor.BufferCount = k_d3d12SwapChainBufferCount;
    nativeDescriptor.Scaling = DXGI_SCALING_STRETCH;
    nativeDescriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    nativeDescriptor.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    nativeDescriptor.Flags = isTearingEnabled ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    const HRESULT creationResult = a_functions.createSwapChainForWindow(
        a_factory, a_queue, a_descriptor.window, &nativeDescriptor, baseSwapChain.GetAddressOf());

    if (FAILED(creationResult))
    {
        return fail_native_creation(make_native_error(a_assertContext, k_swapChainCreationFailed,
                                                      "DXGI Swap Chain creation failed", "DXGI", creationResult),
                                    a_failureHandler,
                                    make_failure_resources(baseSwapChain.Get(), swapChain.Get(), backBuffers),
                                    a_assertContext);
    }

    const HRESULT associationResult = a_functions.disableAltEnter(a_factory, a_descriptor.window);

    if (FAILED(associationResult))
    {
        return fail_native_creation(
            make_native_error(a_assertContext, k_altEnterPolicyFailed, "DXGI Alt+Enter policy could not be disabled",
                              "DXGI", associationResult),
            a_failureHandler, make_failure_resources(baseSwapChain.Get(), swapChain.Get(), backBuffers),
            a_assertContext);
    }

    const HRESULT interfaceResult = a_functions.querySwapChain3(baseSwapChain.Get(), swapChain.GetAddressOf());

    if (FAILED(interfaceResult))
    {
        return fail_native_creation(
            make_native_error(a_assertContext, k_swapChainInterfaceFailed, "IDXGISwapChain3 interface is unavailable",
                              "DXGI", interfaceResult),
            a_failureHandler, make_failure_resources(baseSwapChain.Get(), swapChain.Get(), backBuffers),
            a_assertContext);
    }

    constexpr LPCWSTR backBufferNames[k_d3d12SwapChainBufferCount] = {
        L"CueEngine D3D12 Swap Chain Back Buffer 0",
        L"CueEngine D3D12 Swap Chain Back Buffer 1",
    };

    for (std::uint32_t index = 0; index < k_d3d12SwapChainBufferCount; ++index)
    {
        const HRESULT bufferResult =
            a_functions.getBackBuffer(swapChain.Get(), index, backBuffers[index].GetAddressOf());

        if (FAILED(bufferResult))
        {
            return fail_native_creation(
                make_native_error(a_assertContext, k_backBufferAcquisitionFailed,
                                  "D3D12 Swap Chain Back Buffer acquisition failed", "DXGI", bufferResult),
                a_failureHandler, make_failure_resources(baseSwapChain.Get(), swapChain.Get(), backBuffers),
                a_assertContext);
        }

        const HRESULT nameResult = a_functions.setObjectName(backBuffers[index].Get(), backBufferNames[index]);

        if (FAILED(nameResult))
        {
            return fail_native_creation(
                make_native_error(a_assertContext, k_backBufferNameFailed,
                                  "D3D12 Back Buffer diagnostic name could not be set", "D3D12", nameResult),
                a_failureHandler, make_failure_resources(baseSwapChain.Get(), swapChain.Get(), backBuffers),
                a_assertContext);
        }
    }

    const std::uint32_t currentIndex = a_functions.getCurrentBackBufferIndex(swapChain.Get());

    if (currentIndex >= k_d3d12SwapChainBufferCount)
    {
        return Result<D3d12SwapChainState>::failure(
            make_error(a_assertContext, k_invalidBackBufferIndex, "DXGI Current Back Buffer index is out of range"));
    }

    Result<void> logResult =
        log_swap_chain(a_descriptor, currentIndex, isTearingSupported, isTearingEnabled, a_assertContext);

    if (!logResult)
    {
        return Result<D3d12SwapChainState>::failure(std::move(*logResult.try_error()));
    }

    D3d12SwapChainState state(std::move(swapChain), std::move(backBuffers), a_descriptor, currentIndex,
                              isTearingSupported, isTearingEnabled, a_functions, a_failureHandler, a_assertContext);
    return Result<D3d12SwapChainState>::success(std::move(state));
}
} // namespace cue
