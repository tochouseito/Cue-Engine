#pragma once

#include <Cue/Foundation/Capability.h>
#include <Cue/Foundation/Result.h>
#include <Cue/Project/Descriptor.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cue
{
class AssertContext;

/// @brief Project要件と実行環境の対応関係をPlatform非依存で識別するCapability
enum class ProjectCapability : std::uint8_t
{
    Baseline3D,
    WaveOperations,
    EnhancedBarriers,
    RayTracing,
    MeshShader,
    VariableRateShading,
    SamplerFeedback
};

/// @brief 実行に必須の要件とFallback可能な推奨要件を区別する強度
enum class CapabilityRequirementKind : std::uint8_t
{
    Required,
    Preferred
};

/// @brief Projectが要求するFeatureと必要に応じた最小Version
struct ProjectCapabilityRequirement final
{
    ProjectCapability capability;
    CapabilityRequirementKind kind;
    std::optional<CapabilityVersion> minimumVersion;
};

/// @brief 現在環境で観測したHardware・Engine実装・Runtime状態とVersion
struct ProjectCapabilityObservation final
{
    ProjectCapability capability;
    CapabilityState state;
    std::optional<CapabilityVersion> version;
};

/// @brief 重複Capabilityを含まないVersion 1のProject要件集合
class ProjectCapabilityProfile final
{
  public:
    /// @brief 所有要件を欠くProfileの暗黙生成を防ぐため既定構築を禁止する
    ProjectCapabilityProfile() = delete;
    /// @brief Allocation例外を公開境界へ出さないためCopy構築を禁止する
    ProjectCapabilityProfile(const ProjectCapabilityProfile &) = delete;
    /// @brief Allocation例外を公開境界へ出さないためCopy代入を禁止する
    ProjectCapabilityProfile &operator=(const ProjectCapabilityProfile &) = delete;
    /// @brief 検証済み要件集合の所有権を移動する
    ProjectCapabilityProfile(ProjectCapabilityProfile &&) noexcept = default;
    /// @brief 検証済み要件集合を移動代入する
    ProjectCapabilityProfile &operator=(ProjectCapabilityProfile &&) noexcept = default;
    /// @brief 所有要件Storageを解放する
    ~ProjectCapabilityProfile() = default;

    /// @brief 重複のない要件集合を検証して所有Profileを返す
    [[nodiscard]] static Result<ProjectCapabilityProfile> create(
        std::span<const ProjectCapabilityRequirement> a_requirements,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Profile Wire契約のVersionを返す
    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    /// @brief Projectが宣言した要件を安定した入力順で返す
    [[nodiscard]] std::span<const ProjectCapabilityRequirement> requirements() const noexcept;

  private:
    /// @brief Factoryが検証した所有要件だけからProfileを構築する
    explicit ProjectCapabilityProfile(std::vector<ProjectCapabilityRequirement> &&a_requirements) noexcept;

    std::vector<ProjectCapabilityRequirement> m_requirements;
};

/// @brief 現在SessionのCapability観測値をProjectへ保存しない所有Snapshot
class ProjectCapabilitySnapshot final
{
  public:
    /// @brief 観測元を持たないSnapshotの暗黙生成を防ぐため既定構築を禁止する
    ProjectCapabilitySnapshot() = delete;
    /// @brief Allocation例外を公開境界へ出さないためCopy構築を禁止する
    ProjectCapabilitySnapshot(const ProjectCapabilitySnapshot &) = delete;
    /// @brief Allocation例外を公開境界へ出さないためCopy代入を禁止する
    ProjectCapabilitySnapshot &operator=(const ProjectCapabilitySnapshot &) = delete;
    /// @brief 検証済み観測集合の所有権を移動する
    ProjectCapabilitySnapshot(ProjectCapabilitySnapshot &&) noexcept = default;
    /// @brief 検証済み観測集合を移動代入する
    ProjectCapabilitySnapshot &operator=(ProjectCapabilitySnapshot &&) noexcept = default;
    /// @brief 所有観測Storageを解放する
    ~ProjectCapabilitySnapshot() = default;

    /// @brief 重複のない現在環境の観測集合を検証して所有Snapshotを返す
    [[nodiscard]] static Result<ProjectCapabilitySnapshot> create(
        std::span<const ProjectCapabilityObservation> a_observations,
        const AssertContext &a_assertContext) noexcept;

    /// @brief 現在環境の観測値を安定した入力順で返す
    [[nodiscard]] std::span<const ProjectCapabilityObservation> observations() const noexcept;

  private:
    /// @brief Factoryが検証した所有観測だけからSnapshotを構築する
    explicit ProjectCapabilitySnapshot(std::vector<ProjectCapabilityObservation> &&a_observations) noexcept;

    std::vector<ProjectCapabilityObservation> m_observations;
};

/// @brief Project全体の実行互換性分類
enum class ProjectCompatibilityStatus : std::uint8_t
{
    Compatible,
    Degraded,
    Unsupported,
    Unknown
};

/// @brief UIが診断文へ変換できる互換性判定理由
enum class ProjectCompatibilityReasonCode : std::uint8_t
{
    UnsupportedProjectFormat,
    EngineVersionTooOld,
    EngineVersionTooNew,
    CapabilityNotObserved,
    CapabilityNotQueried,
    CapabilityQueryFailed,
    HardwareUnsupported,
    EngineNotImplemented,
    CapabilityVersionUnknown,
    CapabilityVersionTooLow,
    RuntimeDisabled
};

/// @brief 全体理由とCapability固有理由を同じ型で返す診断値
struct ProjectCompatibilityReason final
{
    ProjectCompatibilityReasonCode code;
    std::optional<ProjectCapability> capability;
    CapabilityRequirementKind requirementKind = CapabilityRequirementKind::Required;
};

/// @brief Project Open可否と独立して返す個別Runtime Feature判定
struct RuntimeCapabilityDecision final
{
    ProjectCapability capability;
    CapabilityRequirementKind requirementKind;
    bool isEnabled;
};

/// @brief 互換性分類、Open可否、理由、Runtime Feature判定を所有する結果
class ProjectCompatibilityReport final
{
  public:
    /// @brief 結果を欠くReportの暗黙生成を防ぐため既定構築を禁止する
    ProjectCompatibilityReport() = delete;
    /// @brief Allocation例外を公開境界へ出さないためCopy構築を禁止する
    ProjectCompatibilityReport(const ProjectCompatibilityReport &) = delete;
    /// @brief Allocation例外を公開境界へ出さないためCopy代入を禁止する
    ProjectCompatibilityReport &operator=(const ProjectCompatibilityReport &) = delete;
    /// @brief 判定結果の所有権を移動する
    ProjectCompatibilityReport(ProjectCompatibilityReport &&) noexcept = default;
    /// @brief 判定結果を移動代入する
    ProjectCompatibilityReport &operator=(ProjectCompatibilityReport &&) noexcept = default;
    /// @brief 所有診断Storageを解放する
    ~ProjectCompatibilityReport() = default;

    /// @brief Project全体の互換性分類を返す
    [[nodiscard]] ProjectCompatibilityStatus status() const noexcept;
    /// @brief FormatとEngine Versionの条件を満たしProjectを開けるか返す
    [[nodiscard]] bool can_open() const noexcept;
    /// @brief UIまたはLogが意味付けする理由一覧を返す
    [[nodiscard]] std::span<const ProjectCompatibilityReason> reasons() const noexcept;
    /// @brief Project Open可否とは独立したRuntime Feature判定を返す
    [[nodiscard]] std::span<const RuntimeCapabilityDecision> runtime_decisions() const noexcept;

  private:
    friend Result<ProjectCompatibilityReport> evaluate_project_compatibility(
        std::uint32_t, std::uint32_t, const EngineCompatibility &, const EngineVersion &,
        const ProjectCapabilityProfile &, const ProjectCapabilitySnapshot &, const AssertContext &) noexcept;

    /// @brief Evaluatorが完成させた互換性判定を所有Reportへ束ねる
    ProjectCompatibilityReport(ProjectCompatibilityStatus a_status, bool a_canOpen,
                               std::vector<ProjectCompatibilityReason> &&a_reasons,
                               std::vector<RuntimeCapabilityDecision> &&a_runtimeDecisions) noexcept;

    std::vector<ProjectCompatibilityReason> m_reasons;
    std::vector<RuntimeCapabilityDecision> m_runtimeDecisions;
    ProjectCompatibilityStatus m_status;
    bool m_canOpen;
};

/// @brief Project Format・Engine Version・Capability要件を現在環境と比較して診断可能な結果を返す
[[nodiscard]] Result<ProjectCompatibilityReport> evaluate_project_compatibility(
    std::uint32_t a_projectFormatVersion, std::uint32_t a_supportedProjectFormatVersion,
    const EngineCompatibility &a_engineCompatibility, const EngineVersion &a_currentEngineVersion,
    const ProjectCapabilityProfile &a_profile, const ProjectCapabilitySnapshot &a_snapshot,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
