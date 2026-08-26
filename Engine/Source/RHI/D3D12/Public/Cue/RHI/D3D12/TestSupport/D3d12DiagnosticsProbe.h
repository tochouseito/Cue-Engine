#pragma once

#include <Cue/Foundation/Result.h>

namespace cue
{
class AssertContext;

/// @brief BUILD_TESTING時だけ公開するD3D12診断Probe結果
///
/// Native Interfaceを所有せず、呼び出し元がValueとして所有する
struct D3d12DiagnosticsProbeReport final
{
    bool isAllowedByBuild;
    bool isDebugInterfaceAvailable;
    bool isDredInterfaceAvailable;
    bool isDebugLayerEnabled;
    bool isDredEnabled;
};

/// @brief 現在のBuildでD3D12診断が許可される場合はtrue
[[nodiscard]] bool are_d3d12_diagnostics_allowed_for_probe() noexcept;

/// @brief 空の DRED UTF-16 名が既定名へ Fall Back する既存契約を検証する
[[nodiscard]] bool verify_d3d12_empty_dred_name_fallback_for_probe(
    const AssertContext &a_assertContext) noexcept;

/// @brief 診断無効設定をDevice生成前経路へ適用して結果を返す
/// @param a_assertContext 呼出中有効な非所有診断Context
/// @return 成功時は適用結果、失敗時は診断可能なError
///
/// 生成Threadで呼び出す。失敗時はD3D12 Deviceを生成していない
[[nodiscard]] Result<D3d12DiagnosticsProbeReport> probe_disabled_d3d12_diagnostics(
    const AssertContext &a_assertContext) noexcept;

/// @brief Standard ValidationとDRED設定をDevice生成前経路へ適用して結果を返す
/// @param a_assertContext 呼出中有効な非所有診断Context
/// @return 成功時はRuntime Interface可用性と適用結果、失敗時は診断可能なError
///
/// 生成Threadで呼び出す。Releaseの禁止設定ではNative APIを呼ばず失敗する
[[nodiscard]] Result<D3d12DiagnosticsProbeReport> probe_configured_d3d12_diagnostics(
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
