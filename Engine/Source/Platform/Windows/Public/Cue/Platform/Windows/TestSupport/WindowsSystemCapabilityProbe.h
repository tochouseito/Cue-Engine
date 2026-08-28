#pragma once

namespace cue
{
/// @brief Synthetic CPU・OS入力でSystem Capability Mappingの全状態を検証する
[[nodiscard]] bool verify_windows_system_capability_mapping_for_probe() noexcept;
} // namespace cue
