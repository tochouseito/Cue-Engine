// D3D12 Debug Layer、InfoQueue、DRED、Live Object の設定と収集を統括する内部診断契約
// Device 生成前後で必要な設定時点を分け、Device Removal 原因を Native Object 解放前に保存する

#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/RHI/D3D12/D3d12Backend.h>

#include <string_view>

struct ID3D12Device;

namespace cue
{
class AssertContext;

struct D3d12DiagnosticsStatus final
{
    bool isDebugLayerEnabled;
    bool isDredEnabled;
    bool isInfoQueueEnabled;
};

// Process の診断許可だけを副作用なしで問い合わせ、Release Build の既定無効化を一箇所へ集約する
[[nodiscard]] bool are_d3d12_diagnostics_allowed() noexcept;

// Debug Layer と DRED は Device 生成前に有効化する必要があるため、Device Factory 処理より先に呼び出す
[[nodiscard]] Result<D3d12DiagnosticsStatus> configure_d3d12_pre_device_diagnostics(
    const D3d12BackendDescriptor &a_descriptor, const AssertContext &a_assertContext) noexcept;

[[nodiscard]] Result<void> configure_d3d12_info_queue(ID3D12Device *a_device,
                                                      D3d12DiagnosticsStatus &a_status,
                                                      const AssertContext &a_assertContext) noexcept;

/** @brief Debug Layer 有効時に M03 Device の Live Object 診断を出力する */
[[nodiscard]] Result<void> report_d3d12_live_device_objects(
    ID3D12Device *a_device, const D3d12DiagnosticsStatus &a_status,
    const AssertContext &a_assertContext) noexcept;

/**
 * @brief D3D12 Message を追加生成できない静止点で InfoQueue を記録して消費する
 * @param a_device 診断対象の D3D12 Device
 * @param a_status Device へ適用済みの診断状態
 * @param a_context Message へ付与する上位 Context
 * @param a_assertContext Foundation 診断 Context
 * @return 記録と消費に成功した場合は Success、取得または記録に失敗した場合は Error
 * @pre 通常終了では全 Command Queue の対象 Work が Fence 完了済みであること
 * @pre Device Removal では有効な場合に DRED 収集を一度試行した後、Queue と Fence を解放済みであること
 * @pre どちらの経路も他 Thread を含め D3D12 API 呼び出しが停止していること
 */
[[nodiscard]] Result<void> log_d3d12_messages_at_quiescent_point(
    ID3D12Device *a_device, const D3d12DiagnosticsStatus &a_status, std::string_view a_context,
    const AssertContext &a_assertContext) noexcept;

[[nodiscard]] Result<void> collect_d3d12_device_removed_diagnostics(
    ID3D12Device *a_device, const D3d12DiagnosticsStatus &a_status,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
