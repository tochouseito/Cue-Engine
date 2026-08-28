#include <Cue/Foundation/Capability.h>

#include <iterator>

namespace
{
/// @brief CapabilityStateの9個の公開FactoryがADR-0012の有効状態だけを返すか検証する
[[nodiscard]] constexpr bool verify_capability_states() noexcept
{
    using cue::CapabilityEnablement;
    using cue::CapabilityImplementation;
    using cue::CapabilityQueryStatus;
    using cue::CapabilityState;
    using cue::CapabilitySupport;

    const CapabilityState states[] = {
        CapabilityState::not_queried_not_implemented(), CapabilityState::not_queried_implemented(),
        CapabilityState::query_failed_not_implemented(), CapabilityState::query_failed_implemented(),
        CapabilityState::unsupported_not_implemented(), CapabilityState::unsupported_implemented(),
        CapabilityState::supported_not_implemented(), CapabilityState::supported_disabled(),
        CapabilityState::supported_enabled(),
    };
    const CapabilityQueryStatus queries[] = {
        CapabilityQueryStatus::NotQueried, CapabilityQueryStatus::NotQueried, CapabilityQueryStatus::Failed,
        CapabilityQueryStatus::Failed, CapabilityQueryStatus::Succeeded, CapabilityQueryStatus::Succeeded,
        CapabilityQueryStatus::Succeeded, CapabilityQueryStatus::Succeeded, CapabilityQueryStatus::Succeeded,
    };
    const CapabilitySupport support[] = {
        CapabilitySupport::Unknown, CapabilitySupport::Unknown, CapabilitySupport::Unknown,
        CapabilitySupport::Unknown, CapabilitySupport::Unsupported, CapabilitySupport::Unsupported,
        CapabilitySupport::Supported, CapabilitySupport::Supported, CapabilitySupport::Supported,
    };
    const CapabilityImplementation implementations[] = {
        CapabilityImplementation::NotImplemented, CapabilityImplementation::Implemented,
        CapabilityImplementation::NotImplemented, CapabilityImplementation::Implemented,
        CapabilityImplementation::NotImplemented, CapabilityImplementation::Implemented,
        CapabilityImplementation::NotImplemented, CapabilityImplementation::Implemented,
        CapabilityImplementation::Implemented,
    };
    const CapabilityEnablement enablement[] = {
        CapabilityEnablement::NotApplicable, CapabilityEnablement::NotApplicable,
        CapabilityEnablement::NotApplicable, CapabilityEnablement::NotApplicable,
        CapabilityEnablement::NotApplicable, CapabilityEnablement::NotApplicable,
        CapabilityEnablement::NotApplicable, CapabilityEnablement::Disabled, CapabilityEnablement::Enabled,
    };

    for (std::size_t index = 0; index < std::size(states); ++index)
    {
        if (states[index].hardware().query_status() != queries[index] ||
            states[index].hardware().support() != support[index] ||
            states[index].implementation() != implementations[index] || states[index].enablement() != enablement[index])
        {
            return false;
        }
    }
    return cue::CapabilityVersion{1, 10} > cue::CapabilityVersion{1, 9} &&
           cue::CapabilityVersion{2, 0} > cue::CapabilityVersion{1, 99};
}
} // namespace

static_assert(verify_capability_states());

/// @brief Compile-time検証済みCapability契約をTest Processの成功として返す
int main()
{
    return verify_capability_states() ? 0 : 1;
}
