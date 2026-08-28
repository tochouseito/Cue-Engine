#include "SystemCapabilityMapper.h"

namespace
{
constexpr std::uint64_t k_xmmStateMask = 1ULL << 1;
constexpr std::uint64_t k_ymmStateMask = 1ULL << 2;
constexpr std::uint64_t k_avxStateMask = k_xmmStateMask | k_ymmStateMask;

/// @brief Boolean Query結果をSupportedまたはUnsupportedの型付き状態へ変換する
[[nodiscard]] constexpr cue::CapabilitySupportState support_state(bool a_isSupported) noexcept
{
    return a_isSupported ? cue::CapabilitySupportState::supported() : cue::CapabilitySupportState::unsupported();
}
} // namespace

namespace cue::windows_private
{
CpuInstructionCapabilities map_system_instruction_capabilities(const SystemCapabilityProbeInput &a_input) noexcept
{
    if (!a_input.hasCpuQuerySucceeded)
    {
        return {
            CapabilitySupportState::query_failed(),
            CapabilitySupportState::query_failed(),
            CapabilitySupportState::query_failed(),
            CapabilitySupportState::query_failed(),
            CapabilitySupportState::query_failed(),
            CapabilitySupportState::query_failed(),
            CapabilitySupportState::query_failed(),
            CapabilitySupportState::query_failed(),
            CapabilitySupportState::query_failed(),
        };
    }

    // AVX系命令はCPU Bitだけでは実行できず、OSがXMMとYMM Contextを保存する場合だけSupportedにする
    const bool hasAvxContext = a_input.hasOsExtendedState &&
                               (a_input.enabledExtendedStateMask & k_avxStateMask) == k_avxStateMask;
    const bool hasUsableAvx = a_input.hasAvx && hasAvxContext;

    return {
        support_state(a_input.hasSse2),
        support_state(a_input.hasSse3),
        support_state(a_input.hasSsse3),
        support_state(a_input.hasSse41),
        support_state(a_input.hasSse42),
        support_state(hasUsableAvx),
        support_state(a_input.hasAvx2 && hasUsableAvx),
        support_state(a_input.hasFma && hasUsableAvx),
        support_state(hasAvxContext),
    };
}
} // namespace cue::windows_private
