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

[[nodiscard]] Result<void> log_d3d12_messages(ID3D12Device *a_device, const D3d12DiagnosticsStatus &a_status,
                                              std::string_view a_context,
                                              const AssertContext &a_assertContext) noexcept;

[[nodiscard]] Result<void> collect_d3d12_device_removed_diagnostics(
    ID3D12Device *a_device, const D3d12DiagnosticsStatus &a_status,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
