#pragma once

#include <Cue/RHI/GraphicsBackend.h>

#include <memory>

namespace cue
{
/** @brief D3D12 Adapter選択方針 */
enum class D3d12AdapterPolicy
{
    HighPerformanceHardware,
    Warp,
};

/** @brief D3D12 Validation設定 */
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

    /** @brief DREDを有効化する場合はtrue */
    bool isDredEnabled;
};

class AssertContext;

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
};

/**
 * @brief D3D12 Backendの一意所有権を生成する
 * @param a_descriptor Adapterと診断の生成設定
 * @param a_assertContext Backendより長く生存するAssert Context
 * @return 成功時はD3D12 Backendの一意Owner、失敗時は診断可能なError
 *
 * Device生成と実装本体はIssue #45で追加する
 */
[[nodiscard]] Result<std::unique_ptr<D3d12Backend>> create_d3d12_backend(
    const D3d12BackendDescriptor &a_descriptor, AssertContext &a_assertContext) noexcept;
} // namespace cue
