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

/// @brief Production 経路で使用する D3D12 Native API 関数 Table を構築して返す
[[nodiscard]] const D3d12SwapChainNativeFunctions &default_d3d12_swap_chain_native_functions() noexcept;

class D3d12SwapChainState final
{
  public:
    /// @brief D3d12SwapChainState の一意所有を保つため Copy 構築を禁止する
    D3d12SwapChainState(const D3d12SwapChainState &) = delete;
    /// @brief D3d12SwapChainState の一意所有を保つため Copy 代入を禁止する
    D3d12SwapChainState &operator=(const D3d12SwapChainState &) = delete;
    /// @brief D3d12SwapChainState の所有状態を Move 構築し、移動元を安全な空状態へ戻す
    D3d12SwapChainState(D3d12SwapChainState &&a_other) noexcept;
    /// @brief D3d12SwapChainState の所有状態を Move 代入し、代入元を安全な空状態へ移す
    D3d12SwapChainState &operator=(D3d12SwapChainState &&a_other) noexcept;
    /// @brief D3d12SwapChainState が保持する Resource を所有権規則に従って破棄する
    ~D3d12SwapChainState() noexcept;

    /// @brief D3D12 Swap Chain State の Current Back Buffer Index を整合性を保って更新する
    [[nodiscard]] Result<std::uint32_t> refresh_current_back_buffer_index() noexcept;
    /// @brief D3D12 Swap Chain State が保持する Back Buffer を呼び出し元へ返す
    [[nodiscard]] Result<ID3D12Resource *> back_buffer(std::uint32_t a_index) const noexcept;
    /// @brief D3D12 Swap Chain State の Back Buffers を整合性を保って更新する
    [[nodiscard]] Result<D3d12SwapChainBackBuffers> take_back_buffers() noexcept;
    /// @brief D3D12 Swap Chain State を指定 Size へ再構築し、後続処理へ反映する
    [[nodiscard]] Result<void> resize(std::uint32_t a_width, std::uint32_t a_height) noexcept;
    /// @brief D3D12 Swap Chain State を GPU 実行順と Resource State を守って投入する
    [[nodiscard]] Result<D3d12PresentStatus> present() noexcept;
    /// @brief 保持する Native Resource を依存関係と完了条件に従って停止し、安全な解放結果を返す
    [[nodiscard]] Result<void> shutdown() noexcept;

    /// @brief D3D12 Swap Chain State が保持する Width を呼び出し元へ返す
    [[nodiscard]] std::uint32_t width() const noexcept;
    /// @brief D3D12 Swap Chain State が保持する Height を呼び出し元へ返す
    [[nodiscard]] std::uint32_t height() const noexcept;
    /// @brief D3D12 Swap Chain State が保持する Format を呼び出し元へ返す
    [[nodiscard]] DXGI_FORMAT format() const noexcept;
    /// @brief D3D12 Swap Chain State が保持する Buffer Count を呼び出し元へ返す
    [[nodiscard]] std::uint32_t buffer_count() const noexcept;
    /// @brief D3D12 Swap Chain State が保持する Current Back Buffer Index を呼び出し元へ返す
    [[nodiscard]] std::uint32_t current_back_buffer_index() const noexcept;
    /// @brief D3D12 Swap Chain State の VSync Enabled 条件を判定して返す
    [[nodiscard]] bool is_vsync_enabled() const noexcept;
    /// @brief D3D12 Swap Chain State の Tearing Supported 条件を判定して返す
    [[nodiscard]] bool is_tearing_supported() const noexcept;
    /// @brief D3D12 Swap Chain State の Tearing Enabled 条件を判定して返す
    [[nodiscard]] bool is_tearing_enabled() const noexcept;
    /// @brief Swap Chain の全 Back Buffer 取得が完了しているかを返す
    [[nodiscard]] bool has_all_back_buffers() const noexcept;
    /// @brief 安全な解放判断に必要な Native Object が残存しているかを返す
    [[nodiscard]] bool has_native_objects() const noexcept;

  private:
    /// @brief D3D12 Swap Chain State で使用する D3D12 Swap Chain State を生成し、呼び出し元へ返す
    friend Result<D3d12SwapChainState> create_d3d12_swap_chain_state(IDXGIFactory6 *, ID3D12CommandQueue *,
                                                                     const D3d12SwapChainDescriptor &,
                                                                     const AssertContext &,
                                                                     const D3d12SwapChainNativeFunctions &,
                                                                     const D3d12SwapChainFailureHandler &) noexcept;

    /// @brief D3d12SwapChainState を必要な依存と初期状態から構築する
    D3d12SwapChainState(Microsoft::WRL::ComPtr<IDXGISwapChain3> a_swapChain,
                        D3d12SwapChainBackBuffers &&a_backBuffers,
                        const D3d12SwapChainDescriptor &a_descriptor, std::uint32_t a_currentBackBufferIndex,
                        bool a_isTearingSupported, bool a_isTearingEnabled,
                        const D3d12SwapChainNativeFunctions &a_functions,
                        const D3d12SwapChainFailureHandler &a_failureHandler,
                        const AssertContext &a_assertContext) noexcept;

    /// @brief 移動元の Native 所有権と Lifecycle 状態を受け取り、移動元を安全な空状態へ戻す
    void take_from(D3d12SwapChainState &&a_other) noexcept;
    /// @brief D3D12 Swap Chain State の Back Buffers を依存関係と完了条件を守って安全に解放または停止する
    void release_back_buffers() noexcept;
    /// @brief D3D12 Swap Chain State の Back Buffers を所有権と Lifecycle 規則を守って関連付ける
    [[nodiscard]] Result<void> acquire_back_buffers() noexcept;
    /// @brief D3D12 Swap Chain State の Native Failure を規定された順序と失敗規則で処理する
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
