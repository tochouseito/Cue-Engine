#pragma once

#include "D3d12AdapterSelection.h"

#include <Cue/Foundation/Result.h>
#include <Cue/RHI/GraphicsBackend.h>

#include <d3d12.h>

namespace cue
{
class AssertContext;

/// @brief D3D12 Deviceの固定Graphics CapabilityをPlatform非依存Snapshotへ変換する
[[nodiscard]] Result<CapabilityReport> query_d3d12_capability_report(
    ID3D12Device *a_device, const D3d12AdapterReport &a_adapterReport,
    const AssertContext &a_assertContext) noexcept;

namespace d3d12_private
{
/// @brief Testで一度だけ失敗させるOptional Feature Queryを指定する
void set_capability_query_failure_for_probe(D3D12_FEATURE a_feature) noexcept;

/// @brief Test用Optional Feature Query失敗注入を解除する
void clear_capability_query_failure_for_probe() noexcept;

/// @brief TestでNative Query成功後に未知のCapability値を一度だけ注入するFeatureを指定する
void set_capability_query_unknown_value_for_probe(D3D12_FEATURE a_feature) noexcept;

/// @brief Test用未知Capability値注入を解除する
void clear_capability_query_unknown_value_for_probe() noexcept;
} // namespace d3d12_private
} // namespace cue
