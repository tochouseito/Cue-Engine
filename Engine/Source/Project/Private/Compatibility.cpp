#include <Cue/Project/Compatibility.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Project/Error.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
/// @brief 互換性判定中の予期しない例外を追加AllocationなしでFatal境界へ渡す
[[noreturn]] void terminate_compatibility_exception(const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Project compatibility operation failed unexpectedly");
    std::abort();
}

/// @brief 外部入力から不正なCapability列挙値をProfileへ混入させない
[[nodiscard]] constexpr bool is_valid_capability(cue::ProjectCapability a_capability) noexcept
{
    return a_capability >= cue::ProjectCapability::Baseline3D &&
           a_capability <= cue::ProjectCapability::SamplerFeedback;
}

/// @brief 外部入力から不正なRequirement強度をProfileへ混入させない
[[nodiscard]] constexpr bool is_valid_requirement_kind(cue::CapabilityRequirementKind a_kind) noexcept
{
    return a_kind == cue::CapabilityRequirementKind::Required ||
           a_kind == cue::CapabilityRequirementKind::Preferred;
}

/// @brief 全体判定をCompatibleからDegraded、Unknown、Unsupportedの優先順で悪化させる
void raise_status(cue::ProjectCompatibilityStatus &a_status, cue::ProjectCompatibilityStatus a_candidate) noexcept
{
    constexpr auto rank = std::array{
        0U,
        1U,
        3U,
        2U,
    };
    if (rank[static_cast<std::size_t>(a_candidate)] > rank[static_cast<std::size_t>(a_status)])
    {
        a_status = a_candidate;
    }
}

/// @brief 必須要件と推奨要件に応じて未達状態を全体判定へ反映する
void raise_requirement_failure(cue::ProjectCompatibilityStatus &a_status,
                               cue::CapabilityRequirementKind a_kind,
                               cue::ProjectCompatibilityStatus a_requiredStatus) noexcept
{
    raise_status(a_status, a_kind == cue::CapabilityRequirementKind::Required
                               ? a_requiredStatus
                               : cue::ProjectCompatibilityStatus::Degraded);
}

/// @brief Snapshotから指定Capabilityの観測値を非所有で検索する
[[nodiscard]] const cue::ProjectCapabilityObservation *find_observation(
    const cue::ProjectCapabilitySnapshot &a_snapshot, cue::ProjectCapability a_capability) noexcept
{
    for (const cue::ProjectCapabilityObservation &observation : a_snapshot.observations())
    {
        if (observation.capability == a_capability)
        {
            return &observation;
        }
    }
    return nullptr;
}

/// @brief Capability固有の理由をRequirement強度と共に結果へ追加する
void append_capability_reason(std::vector<cue::ProjectCompatibilityReason> &a_reasons,
                              cue::ProjectCompatibilityReasonCode a_code,
                              const cue::ProjectCapabilityRequirement &a_requirement)
{
    a_reasons.push_back(
        {a_code, a_requirement.capability, a_requirement.kind, a_requirement.minimumVersion});
}

/// @brief 一つのProject要件について診断とRuntime有効状態を独立して評価する
void evaluate_requirement(const cue::ProjectCapabilityRequirement &a_requirement,
                          const cue::ProjectCapabilitySnapshot &a_snapshot,
                          cue::ProjectCompatibilityStatus &a_status,
                          std::vector<cue::ProjectCompatibilityReason> &a_reasons,
                          std::vector<cue::RuntimeCapabilityDecision> &a_runtimeDecisions)
{
    const cue::ProjectCapabilityObservation *observation =
        find_observation(a_snapshot, a_requirement.capability);
    if (observation == nullptr)
    {
        append_capability_reason(a_reasons, cue::ProjectCompatibilityReasonCode::CapabilityNotObserved,
                                 a_requirement);
        raise_requirement_failure(a_status, a_requirement.kind, cue::ProjectCompatibilityStatus::Unknown);
        a_runtimeDecisions.push_back({a_requirement.capability, a_requirement.kind, false});
        return;
    }

    const cue::CapabilitySupportState hardware = observation->state.hardware();
    if (hardware.query_status() == cue::CapabilityQueryStatus::NotQueried)
    {
        append_capability_reason(a_reasons, cue::ProjectCompatibilityReasonCode::CapabilityNotQueried,
                                 a_requirement);
        raise_requirement_failure(a_status, a_requirement.kind, cue::ProjectCompatibilityStatus::Unknown);
        a_runtimeDecisions.push_back({a_requirement.capability, a_requirement.kind, false});
        return;
    }
    if (hardware.query_status() == cue::CapabilityQueryStatus::Failed)
    {
        append_capability_reason(a_reasons, cue::ProjectCompatibilityReasonCode::CapabilityQueryFailed,
                                 a_requirement);
        raise_requirement_failure(a_status, a_requirement.kind, cue::ProjectCompatibilityStatus::Unknown);
        a_runtimeDecisions.push_back({a_requirement.capability, a_requirement.kind, false});
        return;
    }
    if (hardware.support() == cue::CapabilitySupport::Unsupported)
    {
        append_capability_reason(a_reasons, cue::ProjectCompatibilityReasonCode::HardwareUnsupported,
                                 a_requirement);
        raise_requirement_failure(a_status, a_requirement.kind, cue::ProjectCompatibilityStatus::Unsupported);
        a_runtimeDecisions.push_back({a_requirement.capability, a_requirement.kind, false});
        return;
    }
    if (observation->state.implementation() == cue::CapabilityImplementation::NotImplemented)
    {
        append_capability_reason(a_reasons, cue::ProjectCompatibilityReasonCode::EngineNotImplemented,
                                 a_requirement);
        raise_requirement_failure(a_status, a_requirement.kind, cue::ProjectCompatibilityStatus::Unsupported);
        a_runtimeDecisions.push_back({a_requirement.capability, a_requirement.kind, false});
        return;
    }
    if (a_requirement.minimumVersion.has_value() && !observation->version.has_value())
    {
        append_capability_reason(a_reasons, cue::ProjectCompatibilityReasonCode::CapabilityVersionUnknown,
                                 a_requirement);
        raise_requirement_failure(a_status, a_requirement.kind, cue::ProjectCompatibilityStatus::Unknown);
        a_runtimeDecisions.push_back({a_requirement.capability, a_requirement.kind, false});
        return;
    }
    if (a_requirement.minimumVersion.has_value() &&
        *observation->version < *a_requirement.minimumVersion)
    {
        append_capability_reason(a_reasons, cue::ProjectCompatibilityReasonCode::CapabilityVersionTooLow,
                                 a_requirement);
        raise_requirement_failure(a_status, a_requirement.kind, cue::ProjectCompatibilityStatus::Unsupported);
        a_runtimeDecisions.push_back({a_requirement.capability, a_requirement.kind, false});
        return;
    }

    const bool isEnabled = observation->state.enablement() == cue::CapabilityEnablement::Enabled;
    if (!isEnabled)
    {
        append_capability_reason(a_reasons, cue::ProjectCompatibilityReasonCode::RuntimeDisabled, a_requirement);
        raise_status(a_status, cue::ProjectCompatibilityStatus::Degraded);
    }
    a_runtimeDecisions.push_back({a_requirement.capability, a_requirement.kind, isEnabled});
}
} // namespace

namespace cue
{
ProjectCapabilityProfile::ProjectCapabilityProfile(
    std::vector<ProjectCapabilityRequirement> &&a_requirements) noexcept
    : m_requirements(std::move(a_requirements))
{
}

Result<ProjectCapabilityProfile> ProjectCapabilityProfile::create(
    std::span<const ProjectCapabilityRequirement> a_requirements,
    const AssertContext &a_assertContext) noexcept
{
    try
    {
        for (std::size_t index = 0U; index < a_requirements.size(); ++index)
        {
            const ProjectCapabilityRequirement &requirement = a_requirements[index];
            if (!is_valid_capability(requirement.capability) || !is_valid_requirement_kind(requirement.kind))
            {
                return Result<ProjectCapabilityProfile>::failure(make_project_error(
                    a_assertContext, ProjectError::InvalidCompatibilityInput,
                    "Project capability requirement contains an invalid enum value"));
            }
            for (std::size_t other = index + 1U; other < a_requirements.size(); ++other)
            {
                if (requirement.capability == a_requirements[other].capability)
                {
                    return Result<ProjectCapabilityProfile>::failure(make_project_error(
                        a_assertContext, ProjectError::InvalidCompatibilityInput,
                        "Project capability requirement is duplicated"));
                }
            }
        }
        std::vector<ProjectCapabilityRequirement> requirements(a_requirements.begin(), a_requirements.end());
        return Result<ProjectCapabilityProfile>::success(ProjectCapabilityProfile(std::move(requirements)));
    }
    catch (...)
    {
        terminate_compatibility_exception(a_assertContext);
    }
}

std::uint32_t ProjectCapabilityProfile::schema_version() const noexcept
{
    return 1U;
}

std::span<const ProjectCapabilityRequirement> ProjectCapabilityProfile::requirements() const noexcept
{
    return m_requirements;
}

ProjectCapabilitySnapshot::ProjectCapabilitySnapshot(
    std::vector<ProjectCapabilityObservation> &&a_observations) noexcept
    : m_observations(std::move(a_observations))
{
}

Result<ProjectCapabilitySnapshot> ProjectCapabilitySnapshot::create(
    std::span<const ProjectCapabilityObservation> a_observations,
    const AssertContext &a_assertContext) noexcept
{
    try
    {
        for (std::size_t index = 0U; index < a_observations.size(); ++index)
        {
            if (!is_valid_capability(a_observations[index].capability))
            {
                return Result<ProjectCapabilitySnapshot>::failure(make_project_error(
                    a_assertContext, ProjectError::InvalidCompatibilityInput,
                    "Project capability observation contains an invalid enum value"));
            }
            for (std::size_t other = index + 1U; other < a_observations.size(); ++other)
            {
                if (a_observations[index].capability == a_observations[other].capability)
                {
                    return Result<ProjectCapabilitySnapshot>::failure(make_project_error(
                        a_assertContext, ProjectError::InvalidCompatibilityInput,
                        "Project capability observation is duplicated"));
                }
            }
        }
        std::vector<ProjectCapabilityObservation> observations(a_observations.begin(), a_observations.end());
        return Result<ProjectCapabilitySnapshot>::success(ProjectCapabilitySnapshot(std::move(observations)));
    }
    catch (...)
    {
        terminate_compatibility_exception(a_assertContext);
    }
}

std::span<const ProjectCapabilityObservation> ProjectCapabilitySnapshot::observations() const noexcept
{
    return m_observations;
}

ProjectCompatibilityReport::ProjectCompatibilityReport(
    ProjectCompatibilityStatus a_status, bool a_canOpen,
    std::vector<ProjectCompatibilityReason> &&a_reasons,
    std::vector<RuntimeCapabilityDecision> &&a_runtimeDecisions) noexcept
    : m_reasons(std::move(a_reasons)), m_runtimeDecisions(std::move(a_runtimeDecisions)), m_status(a_status),
      m_canOpen(a_canOpen)
{
}

ProjectCompatibilityStatus ProjectCompatibilityReport::status() const noexcept
{
    return m_status;
}

bool ProjectCompatibilityReport::can_open() const noexcept
{
    return m_canOpen;
}

std::span<const ProjectCompatibilityReason> ProjectCompatibilityReport::reasons() const noexcept
{
    return m_reasons;
}

std::span<const RuntimeCapabilityDecision> ProjectCompatibilityReport::runtime_decisions() const noexcept
{
    return m_runtimeDecisions;
}

Result<ProjectCompatibilityReport> evaluate_project_compatibility(
    std::uint32_t a_projectFormatVersion, std::uint32_t a_supportedProjectFormatVersion,
    const EngineCompatibility &a_engineCompatibility, const EngineVersion &a_currentEngineVersion,
    const ProjectCapabilityProfile &a_profile, const ProjectCapabilitySnapshot &a_snapshot,
    const AssertContext &a_assertContext) noexcept
{
    try
    {
        if (a_projectFormatVersion == 0U || a_supportedProjectFormatVersion == 0U ||
            (a_engineCompatibility.maximumExclusive.has_value() &&
             *a_engineCompatibility.maximumExclusive <= a_engineCompatibility.minimum))
        {
            return Result<ProjectCompatibilityReport>::failure(make_project_error(
                a_assertContext, ProjectError::InvalidCompatibilityInput,
                "Project compatibility version input is invalid"));
        }
        ProjectCompatibilityStatus status = ProjectCompatibilityStatus::Compatible;
        bool canOpen = true;
        std::vector<ProjectCompatibilityReason> reasons;
        std::vector<RuntimeCapabilityDecision> runtimeDecisions;
        reasons.reserve(a_profile.requirements().size() + 2U);
        runtimeDecisions.reserve(a_profile.requirements().size());

        if (a_projectFormatVersion != a_supportedProjectFormatVersion)
        {
            reasons.push_back({ProjectCompatibilityReasonCode::UnsupportedProjectFormat, std::nullopt,
                               CapabilityRequirementKind::Required});
            raise_status(status, ProjectCompatibilityStatus::Unsupported);
            canOpen = false;
        }
        if (a_currentEngineVersion < a_engineCompatibility.minimum)
        {
            reasons.push_back({ProjectCompatibilityReasonCode::EngineVersionTooOld, std::nullopt,
                               CapabilityRequirementKind::Required});
            raise_status(status, ProjectCompatibilityStatus::Unsupported);
            canOpen = false;
        }
        if (a_engineCompatibility.maximumExclusive.has_value() &&
            a_currentEngineVersion >= *a_engineCompatibility.maximumExclusive)
        {
            reasons.push_back({ProjectCompatibilityReasonCode::EngineVersionTooNew, std::nullopt,
                               CapabilityRequirementKind::Required});
            raise_status(status, ProjectCompatibilityStatus::Unsupported);
            canOpen = false;
        }

        for (const ProjectCapabilityRequirement &requirement : a_profile.requirements())
        {
            evaluate_requirement(requirement, a_snapshot, status, reasons, runtimeDecisions);
        }

        return Result<ProjectCompatibilityReport>::success(ProjectCompatibilityReport(
            status, canOpen, std::move(reasons), std::move(runtimeDecisions)));
    }
    catch (...)
    {
        terminate_compatibility_exception(a_assertContext);
    }
}
} // namespace cue
