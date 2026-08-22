#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>
#include <Cue/RHI/GraphicsBackend.h>

#include <cstdint>
#include <string>

namespace cue
{
class AssertContext;

/** @brief BUILD_TESTING時だけ公開するDXGI Adapter選択Probe結果 */
struct D3d12AdapterSelectionProbeReport final
{
    std::string adapterName;
    std::uint64_t dedicatedVideoMemoryBytes;
    std::uint32_t vendorId;
    std::uint32_t deviceId;
    GraphicsAdapterKind adapterKind;
    bool isFeatureLevel12_0;
};

/** @brief 強制したDebug Factory fallbackの呼出情報 */
struct D3d12FactoryFallbackProbeReport final
{
    bool requestedDebugFactory;
    bool retriedWithoutDebug;
};

/**
 * @brief 指定PolicyでDXGI Adapterを選択し、Native所有権を解放してValueだけを返す
 * @param a_policy Hardwareまたは明示WARPの選択Policy
 * @param a_assertContext 呼出中有効な非所有診断Context
 * @return 選択成功時はAdapter情報、利用可能候補がない場合は診断可能なError
 */
[[nodiscard]] Result<D3d12AdapterSelectionProbeReport> probe_d3d12_adapter_selection(
    D3d12AdapterPolicy a_policy, const AssertContext &a_assertContext) noexcept;

/** @brief Software候補と未対応Hardware候補をSkipするProduction選択Logicを検証する */
[[nodiscard]] bool verify_d3d12_unsupported_candidate_skip_for_probe() noexcept;

/** @brief Debug component不足を強制してFactory fallback本体を検証する */
[[nodiscard]] Result<D3d12FactoryFallbackProbeReport> probe_d3d12_factory_fallback(
    const AssertContext &a_assertContext) noexcept;

/** @brief Adapter metadataとDevice由来UMA値のCapability変換を検証する */
[[nodiscard]] bool verify_d3d12_capability_mapping_for_probe(const AssertContext &a_assertContext) noexcept;
} // namespace cue
