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

    /// @brief 重複のない要件集合を検証して入力と独立した所有Profileを返す
    /// @param a_requirements 呼び出し中だけ借用し、成功時は全要素を返却Profileへ複製する要件列
    /// @param a_assertContext Error生成と回復不能失敗に使用し呼び出し完了まで有効な非所有診断Context
    /// @return 不正enumまたは重複CapabilityならInvalidCompatibilityInput、それ以外は要件を所有するProfile
    /// @details 共有可変状態を使用しないため独立した入力による並行呼び出しを許可する。ただし共有AssertContextの参照先は
    /// 既存のLoggerとFatalHandlerのThread Safety契約を満たす必要がある。Allocation失敗を含む予期しない例外では
    /// a_assertContextのFatalHandlerを呼び出して終了し、この関数からは戻らない
    [[nodiscard]] static Result<ProjectCapabilityProfile> create(
        std::span<const ProjectCapabilityRequirement> a_requirements,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Profile Wire契約のVersionを失敗なく値として返す
    /// @return Profile Storageと寿命に依存しないschema version 1の値
    /// @details 同一InstanceがMove、Move代入、破棄されない間は複数Threadからの並行Readを許可する
    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    /// @brief Projectが宣言した要件をProfile所有の非変更Viewとして安定した入力順で返す
    /// @return このInstanceが所有し、このInstanceのMove、Move代入、破棄時に無効化される要件View
    /// @details 同一InstanceがMove、Move代入、破棄されない間は複数Threadからの並行Readを許可する
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

    /// @brief 重複のない現在環境の観測集合を検証して入力と独立した所有Snapshotを返す
    /// @param a_observations 呼び出し中だけ借用し、成功時は全要素を返却Snapshotへ複製する観測列
    /// @param a_assertContext Error生成と回復不能失敗に使用し呼び出し完了まで有効な非所有診断Context
    /// @return 不正enumまたは重複CapabilityならInvalidCompatibilityInput、それ以外は観測値を所有するSnapshot
    /// @details 共有可変状態を使用しないため独立した入力による並行呼び出しを許可する。ただし共有AssertContextの参照先は
    /// 既存のLoggerとFatalHandlerのThread Safety契約を満たす必要がある。Allocation失敗を含む予期しない例外では
    /// a_assertContextのFatalHandlerを呼び出して終了し、この関数からは戻らない
    [[nodiscard]] static Result<ProjectCapabilitySnapshot> create(
        std::span<const ProjectCapabilityObservation> a_observations,
        const AssertContext &a_assertContext) noexcept;

    /// @brief 現在環境の観測値をSnapshot所有の非変更Viewとして安定した入力順で返す
    /// @return このInstanceが所有し、このInstanceのMove、Move代入、破棄時に無効化される観測View
    /// @details 同一InstanceがMove、Move代入、破棄されない間は複数Threadからの並行Readを許可する
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
    std::optional<CapabilityVersion> minimumVersion;
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

    /// @brief Project全体の互換性分類を値として返す
    /// @details 同一InstanceがMove、Move代入、破棄されない間は複数Threadからの並行Readを許可する
    [[nodiscard]] ProjectCompatibilityStatus status() const noexcept;
    /// @brief FormatとEngine Versionの条件を満たしProjectを開けるか値として返す
    /// @details 同一InstanceがMove、Move代入、破棄されない間は複数Threadからの並行Readを許可する
    [[nodiscard]] bool can_open() const noexcept;
    /// @brief UIまたはLogが意味付けする理由一覧をReport所有の非変更Viewとして返す
    /// @return このInstanceが所有し、このInstanceのMove、Move代入、破棄時に無効化される理由View
    /// @details 同一InstanceがMove、Move代入、破棄されない間は複数Threadからの並行Readを許可する
    [[nodiscard]] std::span<const ProjectCompatibilityReason> reasons() const noexcept;
    /// @brief Project Open可否とは独立したRuntime Feature判定をReport所有の非変更Viewとして返す
    /// @return このInstanceが所有し、このInstanceのMove、Move代入、破棄時に無効化されるRuntime判定View
    /// @details 同一InstanceがMove、Move代入、破棄されない間は複数Threadからの並行Readを許可する
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

/// @brief Project Format・Engine Version・Capability要件を現在環境と比較して所有Reportを返す
/// @param a_projectFormatVersion 読み込み対象Project DescriptorのFormat Version
/// @param a_supportedProjectFormatVersion 現在Buildが直接解釈できるFormat Version
/// @param a_engineCompatibility Projectが許容するEngine Version範囲で呼び出し完了まで有効な非所有参照
/// @param a_currentEngineVersion 現在BuildのEngine Versionで呼び出し完了まで有効な非所有参照
/// @param a_profile Project要件の所有元で呼び出し完了まで有効な非所有参照
/// @param a_snapshot 現在Sessionの観測値所有元で呼び出し完了まで有効な非所有参照
/// @param a_assertContext Error生成と回復不能失敗に使用し呼び出し完了まで有効な非所有診断Context
/// @return 入力Versionが0またはEngine範囲が不正ならInvalidCompatibilityInput、それ以外は入力と独立した所有Report
/// @details 共有可変状態を使用しないため独立した入力による並行呼び出しを許可する。ただし共有AssertContextの参照先は
/// 既存のLoggerとFatalHandlerのThread Safety契約を満たす必要がある。入力値またはProject Dataを変更せず、Allocation失敗を含む
/// 予期しない例外ではa_assertContextのFatalHandlerを呼び出して終了し、この関数からは戻らない
[[nodiscard]] Result<ProjectCompatibilityReport> evaluate_project_compatibility(
    std::uint32_t a_projectFormatVersion, std::uint32_t a_supportedProjectFormatVersion,
    const EngineCompatibility &a_engineCompatibility, const EngineVersion &a_currentEngineVersion,
    const ProjectCapabilityProfile &a_profile, const ProjectCapabilitySnapshot &a_snapshot,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue
