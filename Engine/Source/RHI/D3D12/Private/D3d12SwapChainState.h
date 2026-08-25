// DXGI Swap Chain と Back Buffer を所有し、Present と Resize の Native Lifecycle を管理する内部状態
// Tearing 対応能力と実際の有効状態を分け、VSync 設定に応じた Present 条件を一箇所で決定する

#pragma once

#include <Cue/Foundation/Result.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

namespace cue
{
class AssertContext;
class Error;

constexpr std::uint32_t k_d3d12SwapChainBufferCount = 2;
using D3d12SwapChainBackBuffers =
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, k_d3d12SwapChainBufferCount>;

struct D3d12SwapChainDescriptor final
{
    HWND window;
    std::uint32_t width;
    std::uint32_t height;
    DXGI_FORMAT format;
    bool isVsyncEnabled;
};

struct D3d12SwapChainNativeFunctions final
{
    HRESULT (*checkTearingSupport)(IDXGIFactory6 *, BOOL *) noexcept;
    HRESULT (*createSwapChainForWindow)(IDXGIFactory6 *, ID3D12CommandQueue *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                        IDXGISwapChain1 **) noexcept;
    HRESULT (*disableAltEnter)(IDXGIFactory6 *, HWND) noexcept;
    HRESULT (*querySwapChain3)(IDXGISwapChain1 *, IDXGISwapChain3 **) noexcept;
    HRESULT (*getBackBuffer)(IDXGISwapChain3 *, std::uint32_t, ID3D12Resource **) noexcept;
    std::uint32_t (*getCurrentBackBufferIndex)(IDXGISwapChain3 *) noexcept;
    HRESULT (*setObjectName)(ID3D12Object *, LPCWSTR) noexcept;
    HRESULT (*resizeBuffers)(IDXGISwapChain3 *, std::uint32_t, std::uint32_t, std::uint32_t, DXGI_FORMAT,
                             std::uint32_t) noexcept;
    HRESULT (*present)(IDXGISwapChain3 *, UINT, UINT) noexcept;
};

struct D3d12SwapChainFailureResources final
{
    IDXGISwapChain1 *baseSwapChain;
    IDXGISwapChain3 *swapChain;
    std::array<ID3D12Resource *, k_d3d12SwapChainBufferCount> backBuffers;
};

struct D3d12SwapChainFailureHandler final
{
    void *owner;
    Result<void> (*handleNativeFailure)(void *, Error &&, const D3d12SwapChainFailureResources &) noexcept;
};

enum class D3d12PresentStatus
{
    Presented,
    Occluded,
};

[[nodiscard]] const D3d12SwapChainNativeFunctions &default_d3d12_swap_chain_native_functions() noexcept;

class D3d12SwapChainState final
{
  public:
    D3d12SwapChainState(const D3d12SwapChainState &) = delete;
    D3d12SwapChainState &operator=(const D3d12SwapChainState &) = delete;
    D3d12SwapChainState(D3d12SwapChainState &&a_other) noexcept;
    D3d12SwapChainState &operator=(D3d12SwapChainState &&a_other) noexcept;
    ~D3d12SwapChainState() noexcept;

    [[nodiscard]] Result<std::uint32_t> refresh_current_back_buffer_index() noexcept;
    [[nodiscard]] Result<ID3D12Resource *> back_buffer(std::uint32_t a_index) const noexcept;
    [[nodiscard]] Result<D3d12SwapChainBackBuffers> take_back_buffers() noexcept;
    [[nodiscard]] Result<void> resize(std::uint32_t a_width, std::uint32_t a_height) noexcept;
    [[nodiscard]] Result<D3d12PresentStatus> present() noexcept;
    [[nodiscard]] Result<void> shutdown() noexcept;

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] DXGI_FORMAT format() const noexcept;
    [[nodiscard]] std::uint32_t buffer_count() const noexcept;
    [[nodiscard]] std::uint32_t current_back_buffer_index() const noexcept;
    [[nodiscard]] bool is_vsync_enabled() const noexcept;
    [[nodiscard]] bool is_tearing_supported() const noexcept;
    [[nodiscard]] bool is_tearing_enabled() const noexcept;
    [[nodiscard]] bool has_all_back_buffers() const noexcept;
    [[nodiscard]] bool has_native_objects() const noexcept;

  private:
    friend Result<D3d12SwapChainState> create_d3d12_swap_chain_state(IDXGIFactory6 *, ID3D12CommandQueue *,
                                                                     const D3d12SwapChainDescriptor &,
                                                                     const AssertContext &,
                                                                     const D3d12SwapChainNativeFunctions &,
                                                                     const D3d12SwapChainFailureHandler &) noexcept;

    D3d12SwapChainState(Microsoft::WRL::ComPtr<IDXGISwapChain3> a_swapChain,
                        D3d12SwapChainBackBuffers &&a_backBuffers,
                        const D3d12SwapChainDescriptor &a_descriptor, std::uint32_t a_currentBackBufferIndex,
                        bool a_isTearingSupported, bool a_isTearingEnabled,
                        const D3d12SwapChainNativeFunctions &a_functions,
                        const D3d12SwapChainFailureHandler &a_failureHandler,
                        const AssertContext &a_assertContext) noexcept;

    void take_from(D3d12SwapChainState &&a_other) noexcept;
    void release_back_buffers() noexcept;
    [[nodiscard]] Result<void> acquire_back_buffers() noexcept;
    [[nodiscard]] Result<void> classify_native_failure(Error &&a_error) noexcept;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    D3d12SwapChainBackBuffers m_backBuffers;
    D3d12SwapChainNativeFunctions m_functions;
    D3d12SwapChainFailureHandler m_failureHandler;
    const AssertContext *m_assertContext;
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_currentBackBufferIndex;
    DXGI_FORMAT m_format;
    bool m_isVsyncEnabled;
    bool m_isTearingSupported;
    bool m_isTearingEnabled;
};

[[nodiscard]] Result<D3d12SwapChainState> create_d3d12_swap_chain_state(
    IDXGIFactory6 *a_factory, ID3D12CommandQueue *a_queue, const D3d12SwapChainDescriptor &a_descriptor,
    const AssertContext &a_assertContext,
    const D3d12SwapChainNativeFunctions &a_functions = default_d3d12_swap_chain_native_functions(),
    const D3d12SwapChainFailureHandler &a_failureHandler = {}) noexcept;
} // namespace cue
