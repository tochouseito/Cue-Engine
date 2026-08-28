#pragma once

#include <Cue/Platform/SystemCapabilities.h>

#include <cstdint>

namespace cue::windows_private
{
/// @brief Native Query一時値を持たずSynthetic Testでも再現可能なCPU Query入力
struct SystemCapabilityProbeInput final
{
    bool hasCpuQuerySucceeded;
    bool hasSse2;
    bool hasSse3;
    bool hasSsse3;
    bool hasSse41;
    bool hasSse42;
    bool hasAvx;
    bool hasAvx2;
    bool hasFma;
    bool hasOsExtendedState;
    std::uint64_t enabledExtendedStateMask;
};

/// @brief Synthetic可能なCPUとOS入力から実行可能なInstruction Capabilityを構築する
[[nodiscard]] CpuInstructionCapabilities map_system_instruction_capabilities(
    const SystemCapabilityProbeInput &a_input) noexcept;
} // namespace cue::windows_private
