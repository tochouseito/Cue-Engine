#pragma once

#include <Cue/Foundation/Result.h>

#include <cstdint>

namespace cue
{
class AssertContext;
class D3d12Backend;

struct D3d12SwapChainProbeReport final
{
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t bufferCount;
    std::uint32_t currentBackBufferIndex;
    std::uint64_t infoQueueErrorCount;
    bool descriptorShapeIsValid;
    bool backBuffersAreAvailable;
    bool altEnterWasDisabled;
    bool isVsyncEnabled;
    bool isTearingSupported;
    bool isTearingEnabled;
    bool diagnosticsAvailable;
};

/** @brief 実Win32 WindowとWARP QueueでSwap Chain生成・解放を検証する */
[[nodiscard]] Result<D3d12SwapChainProbeReport> probe_d3d12_swap_chain(const void *a_nativeWindow,
                                                                       std::uint32_t a_width, std::uint32_t a_height,
                                                                       bool a_isVsyncEnabled,
                                                                       const AssertContext &a_assertContext) noexcept;

/** @brief VSyncとTearing capabilityからSwap Chain flagが一意に決まることを検証する */
[[nodiscard]] bool verify_d3d12_swap_chain_tearing_matrix_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                                    std::uint32_t a_height,
                                                                    const AssertContext &a_assertContext) noexcept;

/** @brief DXGI生成、Association、Interface、Back Buffer取得、命名失敗のrollbackを検証する */
[[nodiscard]] bool verify_d3d12_swap_chain_faults_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                            std::uint32_t a_height,
                                                            const AssertContext &a_assertContext) noexcept;

/** @brief 診断Log失敗時のrollbackと再生成を検証する */
[[nodiscard]] bool verify_d3d12_swap_chain_log_failure_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                                 std::uint32_t a_height,
                                                                 const AssertContext &a_setupAssertContext,
                                                                 const AssertContext &a_failingAssertContext) noexcept;

/** @brief 無効DescriptorとCurrent Back Buffer Indexを診断Errorとして検証する */
[[nodiscard]] bool verify_d3d12_swap_chain_validation_for_probe(const void *a_nativeWindow, std::uint32_t a_width,
                                                                std::uint32_t a_height,
                                                                const AssertContext &a_assertContext) noexcept;

/** @brief Test用にD3D12 Device Removalを発生させ、Backendへ分類させる */
[[nodiscard]] Result<void> force_d3d12_device_removal_for_probe(D3d12Backend &a_backend) noexcept;

/** @brief BackendがDRED収集を試行した回数を返す */
[[nodiscard]] Result<std::uint32_t> d3d12_dred_attempt_count_for_probe(D3d12Backend &a_backend) noexcept;
} // namespace cue
