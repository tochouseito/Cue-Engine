#pragma once

namespace cue
{
class AssertContext;

/** @brief Device生成失敗がNative Error付きで保持されることを検証する */
[[nodiscard]] bool verify_d3d12_device_creation_failure_for_probe(
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
