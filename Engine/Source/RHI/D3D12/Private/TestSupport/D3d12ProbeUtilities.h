#pragma once

#include <cstdint>

struct ID3D12Device;

namespace cue::d3d12_test_private
{
/// @brief D3D12 Device の Info Queue を検査し、Error または Corruption の Message 数を返す
[[nodiscard]] std::uint64_t count_info_queue_errors(ID3D12Device *a_device) noexcept;
} // namespace cue::d3d12_test_private
