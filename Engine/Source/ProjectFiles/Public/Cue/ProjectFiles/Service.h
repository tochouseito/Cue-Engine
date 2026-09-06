#pragma once

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Result.h>
#include <Cue/IO/WorkspaceFilesystem.h>
#include <Cue/Project/Descriptor.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cue::project_files
{
/// @brief Project Descriptorで定義された役割別Area
enum class ProjectFileArea : std::uint8_t
{
    SourceAssets,
    RuntimeAssets,
    Generated,
    Saved
};

/// @brief Project File操作の意味分類
enum class ProjectFileOperationKind : std::uint8_t
{
    DirectoryCreation,
    FileCreation
};

/// @brief Project File操作が到達した処理段階
enum class ProjectFileOperationStage : std::uint8_t
{
    ValidateRequest,
    BindDestination,
    NativePublish,
    Verify,
    Complete
};

/// @brief Project File操作後に確定した観測結果
enum class ProjectFileOperationOutcome : std::uint8_t
{
    Committed,
    NotCommitted,
    CommittedButDurabilityUnknown,
    ReconciliationRequired
};

/// @brief Operation ID発行をPlatformまたは決定的Test Doubleへ分離する境界
class ProjectFileOperationIdSource
{
  public:
    ProjectFileOperationIdSource(const ProjectFileOperationIdSource &) = delete;
    ProjectFileOperationIdSource &operator=(const ProjectFileOperationIdSource &) = delete;
    virtual ~ProjectFileOperationIdSource() = default;

    /// @brief lowercase UUID Version 4候補を返す
    [[nodiscard]] virtual Result<std::string> next_operation_id() noexcept = 0;

  protected:
    ProjectFileOperationIdSource() noexcept = default;
};

/// @brief 用途別Areaの読取り・Mutation権限を固定する不変Policy
class ProjectFileAccessPolicy final
{
  public:
    /// @brief M13 Editor用のSource Assets限定Policyを返す
    [[nodiscard]] static ProjectFileAccessPolicy editor_default() noexcept;

    /// @brief Files UIへ表示可能なAreaか判定する
    [[nodiscard]] bool can_list(ProjectFileArea a_area) const noexcept;
    /// @brief 通常File Mutationを許可するAreaか判定する
    [[nodiscard]] bool can_mutate(ProjectFileArea a_area) const noexcept;

  private:
    ProjectFileAccessPolicy() noexcept = default;
};

/// @brief Operation ID、結果、Primary Error、Cleanup診断、再列挙対象を所有する
class ProjectFileOperationResult final
{
  public:
    ProjectFileOperationResult() = delete;
    ProjectFileOperationResult(const ProjectFileOperationResult &) = delete;
    ProjectFileOperationResult &operator=(const ProjectFileOperationResult &) = delete;
    ProjectFileOperationResult(ProjectFileOperationResult &&) noexcept = default;
    ProjectFileOperationResult &operator=(ProjectFileOperationResult &&) noexcept = default;
    ~ProjectFileOperationResult() = default;

    [[nodiscard]] std::string_view operation_id() const noexcept;
    [[nodiscard]] ProjectFileOperationKind kind() const noexcept;
    [[nodiscard]] ProjectFileArea area() const noexcept;
    [[nodiscard]] std::string_view destination() const noexcept;
    [[nodiscard]] ProjectFileOperationStage stage() const noexcept;
    [[nodiscard]] ProjectFileOperationOutcome outcome() const noexcept;
    [[nodiscard]] const Error *try_primary_error() const noexcept;
    [[nodiscard]] std::span<const Error> secondary_diagnostics() const noexcept;
    [[nodiscard]] std::span<const std::string> rescan_directories() const noexcept;

  private:
    friend class ProjectFileService;
    ProjectFileOperationResult(std::string a_operationId, ProjectFileOperationKind a_kind, ProjectFileArea a_area,
                               std::string a_destination, ProjectFileOperationStage a_stage,
                               ProjectFileOperationOutcome a_outcome, std::optional<Error> a_primaryError,
                               std::vector<Error> a_secondaryDiagnostics,
                               std::vector<std::string> a_rescanDirectories) noexcept;

    std::string m_operationId;
    ProjectFileOperationKind m_kind;
    ProjectFileArea m_area;
    std::string m_destination;
    ProjectFileOperationStage m_stage;
    ProjectFileOperationOutcome m_outcome;
    std::optional<Error> m_primaryError;
    std::vector<Error> m_secondaryDiagnostics;
    std::vector<std::string> m_rescanDirectories;
};

/// @brief Project Root、Area Policy、Operation ID、単一Mutationを所有するApplication Service
///
/// Instanceは作成Thread限定で、移動後も同じOwner Threadからだけ使用する
class ProjectFileService final
{
  public:
    ProjectFileService() = delete;
    ProjectFileService(const ProjectFileService &) = delete;
    ProjectFileService &operator=(const ProjectFileService &) = delete;
    ProjectFileService(ProjectFileService &&) noexcept = default;
    ProjectFileService &operator=(ProjectFileService &&) noexcept = default;
    ~ProjectFileService() = default;

    /// @brief Descriptor SnapshotとRoot-bound Workspaceを検証し、部分Serviceを公開せず構築する
    [[nodiscard]] static Result<ProjectFileService> create(
        const ProjectDescriptor &a_descriptor, std::unique_ptr<WorkspaceFilesystem> a_workspace,
        std::unique_ptr<ProjectFileOperationIdSource> a_operationIdSource,
        const AssertContext &a_assertContext) noexcept;

    [[nodiscard]] const ProjectId &project_id() const noexcept;
    [[nodiscard]] const ProjectRoots &roots() const noexcept;
    [[nodiscard]] const ProjectFileAccessPolicy &access_policy() const noexcept;

    /// @brief 指定AreaへCreate-new FolderをAtomic公開する
    [[nodiscard]] Result<ProjectFileOperationResult> create_directory(ProjectFileArea a_area,
                                                                      RelativePath a_destination) noexcept;

    /// @brief 指定Areaへ初期Content付きFileをAtomic Create-newする
    [[nodiscard]] Result<ProjectFileOperationResult> create_file(ProjectFileArea a_area, RelativePath a_destination,
                                                                 std::span<const std::byte> a_bytes) noexcept;

  private:
    ProjectFileService(ProjectId a_projectId, ProjectRoots a_roots, ProjectFileAccessPolicy a_policy,
                       std::unique_ptr<WorkspaceFilesystem> a_workspace,
                       std::unique_ptr<ProjectFileOperationIdSource> a_operationIdSource,
                       const AssertContext &a_assertContext) noexcept;

    [[nodiscard]] Result<ProjectFileOperationResult> execute_create(ProjectFileOperationKind a_kind,
                                                                    ProjectFileArea a_area, RelativePath a_destination,
                                                                    std::span<const std::byte> a_bytes) noexcept;
    [[nodiscard]] const RelativePath &area_root(ProjectFileArea a_area) const noexcept;

    ProjectId m_projectId;
    ProjectRoots m_roots;
    ProjectFileAccessPolicy m_policy;
    std::unique_ptr<WorkspaceFilesystem> m_workspace;
    std::unique_ptr<ProjectFileOperationIdSource> m_operationIdSource;
    AssertContext m_assertContext;
    std::thread::id m_ownerThread;
    bool m_isBusy = false;
};
} // namespace cue::project_files
