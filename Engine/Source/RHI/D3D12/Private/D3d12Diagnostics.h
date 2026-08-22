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

[[nodiscard]] bool are_d3d12_diagnostics_allowed() noexcept;

[[nodiscard]] Result<D3d12DiagnosticsStatus> configure_d3d12_pre_device_diagnostics(
    const D3d12BackendDescriptor &a_descriptor, const AssertContext &a_assertContext) noexcept;

[[nodiscard]] Result<void> configure_d3d12_info_queue(ID3D12Device *a_device,
                                                      D3d12DiagnosticsStatus &a_status,
                                                      const AssertContext &a_assertContext) noexcept;

/**
 * @brief D3D12 Messageを追加生成できない静止点でInfoQueueを記録して消費します。
 * @param a_device 診断対象のD3D12 Deviceです。
 * @param a_status Deviceへ適用済みの診断状態です。
 * @param a_context Messageへ付与する上位Contextです。
 * @param a_assertContext Foundation診断Contextです。
 * @return 記録と消費に成功した場合はSuccess、取得または記録に失敗した場合はErrorです。
 * @pre 全Command Queueの対象WorkがFence完了済みで、他Threadを含めD3D12 API呼び出しが停止していること。
 */
[[nodiscard]] Result<void> log_d3d12_messages_at_quiescent_point(
    ID3D12Device *a_device, const D3d12DiagnosticsStatus &a_status, std::string_view a_context,
    const AssertContext &a_assertContext) noexcept;

[[nodiscard]] Result<void> collect_d3d12_device_removed_diagnostics(
    ID3D12Device *a_device, const D3d12DiagnosticsStatus &a_status,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
