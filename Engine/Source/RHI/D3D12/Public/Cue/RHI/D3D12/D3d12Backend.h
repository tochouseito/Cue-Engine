#pragma once

#include <Cue/RHI/GraphicsBackend.h>
#include <Cue/RHI/PresentationContext.h>

#include <cstdint>
#include <memory>

namespace cue
{
/** @brief D3D12 Adapter選択方針 */
enum class D3d12AdapterPolicy
{
    HighPerformanceHardware,
    Warp,
};

/**
 * @brief D3D12 Validation設定
 *
 * GpuBasedは高Costのため明示指定時だけ有効化する。ReleaseではDisabled以外を指定すると生成に失敗する
 */
enum class D3d12ValidationMode
{
    Disabled,
    Standard,
    GpuBased,
};

/** @brief D3D12 Backend生成設定 */
struct D3d12BackendDescriptor final
{
    /** @brief Adapter選択方針 */
    D3d12AdapterPolicy adapterPolicy;

    /** @brief Validation設定 */
    D3d12ValidationMode validationMode;

    /** @brief DREDを有効化する場合はtrue。Releaseではtrueを指定すると生成に失敗する */
    bool isDredEnabled;

    /** @brief GPU完了を待つ有限Timeout。1から60,000 msを許可する */
    std::uint32_t gpuWaitTimeoutMilliseconds;
};

class AssertContext;
class D3d12WindowsPresentationAccess;

/**
 * @brief D3D12固有のBackend型識別境界
 *
 * Native Device、Queue、DXGI Factoryを公開せず、生成Thread上で一意所有する
 */
class D3d12Backend : public GraphicsBackend
{
  public:
    /** @brief 明示Shutdown後にD3D12 Backend Ownerを破棄する */
    ~D3d12Backend() noexcept override;

  protected:
    D3d12Backend() = default;

  private:
    friend class D3d12WindowsPresentationAccess;
    friend Result<void> force_d3d12_device_removal_for_probe(D3d12Backend &) noexcept;
    friend Result<std::uint32_t> d3d12_dred_attempt_count_for_probe(D3d12Backend &) noexcept;

    /** @brief Windows Adapter から渡された短命な Native Window 値を同期消費する */
    [[nodiscard]] virtual Result<std::unique_ptr<PresentationContext>> create_windows_presentation(
        const void *a_nativeWindow, std::uint32_t a_width, std::uint32_t a_height,
        const PresentationDescriptor &a_descriptor) noexcept = 0;

    /** @brief Windows Adapter の provenance 診断に使用する非所有 Context を返す */
    [[nodiscard]] virtual const AssertContext &assert_context_for_presentation() const noexcept = 0;
};

/**
 * @brief D3D12 Backendの一意所有権を生成する
 * @param a_descriptor Adapterと診断の生成設定
 * @param a_assertContext Backendより長く生存するAssert Context
 * @return 成功時はD3D12 Backendの一意Owner、失敗時は診断可能なError
 */
[[nodiscard]] Result<std::unique_ptr<D3d12Backend>> create_d3d12_backend(const D3d12BackendDescriptor &a_descriptor,
                                                                         AssertContext &a_assertContext) noexcept;
} // namespace cue
