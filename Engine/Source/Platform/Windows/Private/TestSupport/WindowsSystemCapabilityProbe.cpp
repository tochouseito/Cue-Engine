#include <Cue/Platform/Windows/TestSupport/WindowsSystemCapabilityProbe.h>

#include "../SystemCapabilityMapper.h"

#include <Cue/Foundation/Capability.h>

namespace
{
/// @brief Support状態が期待したQuery結果とHardware対応を保持するか判定する
[[nodiscard]] constexpr bool matches(cue::CapabilitySupportState a_state,
                                     cue::CapabilityQueryStatus a_queryStatus,
                                     cue::CapabilitySupport a_support) noexcept
{
    return a_state.query_status() == a_queryStatus && a_state.support() == a_support;
}
} // namespace

namespace cue
{
bool verify_windows_system_capability_mapping_for_probe() noexcept
{
    const windows_private::SystemCapabilityProbeInput failed = {
        false, false, false, false, false, false, false, false, false, false, 0,
    };
    const CpuInstructionCapabilities failedResult =
        windows_private::map_system_instruction_capabilities(failed);

    if (!matches(failedResult.sse2, CapabilityQueryStatus::Failed, CapabilitySupport::Unknown) ||
        !matches(failedResult.avx, CapabilityQueryStatus::Failed, CapabilitySupport::Unknown) ||
        !matches(failedResult.osExtendedState, CapabilityQueryStatus::Failed, CapabilitySupport::Unknown))
    {
        return false;
    }

    const windows_private::SystemCapabilityProbeInput unsupported = {
        true, false, false, false, false, false, false, false, false, false, 0,
    };
    const CpuInstructionCapabilities unsupportedResult =
        windows_private::map_system_instruction_capabilities(unsupported);
    if (!matches(unsupportedResult.sse2, CapabilityQueryStatus::Succeeded, CapabilitySupport::Unsupported) ||
        !matches(unsupportedResult.avx, CapabilityQueryStatus::Succeeded, CapabilitySupport::Unsupported))
    {
        return false;
    }

    // CPU AVX BitだけではSupportedにせず、OSXSAVEとXMM・YMM保存状態の両方を要求する
    const windows_private::SystemCapabilityProbeInput missingOsState = {
        true, true, true, true, true, true, true, true, true, false, 0,
    };
    const CpuInstructionCapabilities missingOsResult =
        windows_private::map_system_instruction_capabilities(missingOsState);
    if (missingOsResult.sse2.support() != CapabilitySupport::Supported ||
        missingOsResult.avx.support() != CapabilitySupport::Unsupported ||
        missingOsResult.avx2.support() != CapabilitySupport::Unsupported ||
        missingOsResult.fma.support() != CapabilitySupport::Unsupported)
    {
        return false;
    }

    const windows_private::SystemCapabilityProbeInput supported = {
        true, true, true, true, true, true, true, true, true, true, (1ULL << 1) | (1ULL << 2),
    };
    const CpuInstructionCapabilities supportedResult =
        windows_private::map_system_instruction_capabilities(supported);
    return supportedResult.sse2.support() == CapabilitySupport::Supported &&
           supportedResult.sse3.support() == CapabilitySupport::Supported &&
           supportedResult.ssse3.support() == CapabilitySupport::Supported &&
           supportedResult.sse41.support() == CapabilitySupport::Supported &&
           supportedResult.sse42.support() == CapabilitySupport::Supported &&
           supportedResult.avx.support() == CapabilitySupport::Supported &&
           supportedResult.avx2.support() == CapabilitySupport::Supported &&
           supportedResult.fma.support() == CapabilitySupport::Supported &&
           supportedResult.osExtendedState.support() == CapabilitySupport::Supported;
}
} // namespace cue
