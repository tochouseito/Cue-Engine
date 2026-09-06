#include <Cue/ProjectFiles/Service.h>

#include "TrashRecord.h"

#include <Cue/IO/Error.h>
#include <Cue/ProjectFiles/Error.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace
{
constexpr std::size_t k_maximumProjectDescriptorBytes = 1024U * 1024U;
constexpr std::size_t k_maximumTrashRecordBytes = 16U * 1024U * 1024U;

/// @brief noexcept境界でのAllocation失敗をFatal終了へ変換する
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Project file service allocation failed");
    std::abort();
}

class BusyReset final
{
  public:
    /// @brief Scope中の再入Mutationを拒否するBusy状態を開始する
    explicit BusyReset(bool &a_isBusy) noexcept : m_isBusy(&a_isBusy)
    {
        *m_isBusy = true;
    }
    /// @brief 単一Busy状態の解除責務を保つためCopy構築を禁止する
    BusyReset(const BusyReset &) = delete;
    /// @brief 単一Busy状態の解除責務を保つためCopy代入を禁止する
    BusyReset &operator=(const BusyReset &) = delete;
    /// @brief Scope終了時にBusy状態を解除する
    ~BusyReset()
    {
        *m_isBusy = false;
    }

  private:
    bool *m_isBusy;
};

/// @brief IO ErrorとMutation結果をProject File Errorへ分類する
[[nodiscard]] cue::project_files::ProjectFileError classify_project_file_error(
    const cue::Error &a_error, cue::WorkspaceMutationOutcome a_outcome) noexcept
{
    if (a_outcome == cue::WorkspaceMutationOutcome::CommittedButDurabilityUnknown ||
        a_outcome == cue::WorkspaceMutationOutcome::ReconciliationRequired)
    {
        return cue::project_files::ProjectFileError::RecoveryRequired;
    }

    const cue::ErrorCode &code = a_error.root_code();
    if (code.domain() != "Cue.IO")
    {
        return cue::project_files::ProjectFileError::StorageFailure;
    }
    switch (static_cast<cue::IoError>(code.value()))
    {
    case cue::IoError::AlreadyExists:
        return cue::project_files::ProjectFileError::Conflict;
    case cue::IoError::CapacityExceeded:
        return cue::project_files::ProjectFileError::LimitExceeded;
    case cue::IoError::Busy:
        return cue::project_files::ProjectFileError::Busy;
    case cue::IoError::InvalidPath:
    case cue::IoError::OutsideRoot:
    case cue::IoError::NotFound:
    case cue::IoError::TypeMismatch:
    case cue::IoError::UnsupportedEntry:
    case cue::IoError::PreconditionFailed:
        return cue::project_files::ProjectFileError::InvalidRequest;
    case cue::IoError::PermissionDenied:
    case cue::IoError::IoFailure:
    case cue::IoError::DurabilityUnknown:
        return cue::project_files::ProjectFileError::StorageFailure;
    }
    return cue::project_files::ProjectFileError::StorageFailure;
}

/// @brief IO Mutation結果をProject File Operation結果へ変換する
[[nodiscard]] cue::project_files::ProjectFileOperationOutcome convert_outcome(
    cue::WorkspaceMutationOutcome a_outcome) noexcept
{
    switch (a_outcome)
    {
    case cue::WorkspaceMutationOutcome::Committed:
        return cue::project_files::ProjectFileOperationOutcome::Committed;
    case cue::WorkspaceMutationOutcome::NotCommitted:
        return cue::project_files::ProjectFileOperationOutcome::NotCommitted;
    case cue::WorkspaceMutationOutcome::CommittedButDurabilityUnknown:
        return cue::project_files::ProjectFileOperationOutcome::CommittedButDurabilityUnknown;
    case cue::WorkspaceMutationOutcome::ReconciliationRequired:
        return cue::project_files::ProjectFileOperationOutcome::ReconciliationRequired;
    }
    return cue::project_files::ProjectFileOperationOutcome::ReconciliationRequired;
}

/// @brief Error ChainのRootが指定IO分類か判定する
[[nodiscard]] bool is_io_error(const cue::Error &a_error, cue::IoError a_code) noexcept
{
    const cue::ErrorCode &code = a_error.root_code();
    return code.domain() == "Cue.IO" && code.value() == static_cast<std::int64_t>(a_code);
}

/// @brief Area相対Pathの親Locatorを空文字許容で返す
[[nodiscard]] std::string parent_locator(std::string_view a_path, const cue::AssertContext &a_assertContext) noexcept
{
    std::string parent;
    try
    {
        const std::size_t separator = a_path.rfind('/');
        if (separator != std::string_view::npos)
        {
            parent.assign(a_path.substr(0U, separator));
        }
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    return parent;
}

/// @brief Fingerprintが保持する全Regular FileのLogical Byte数を飽和加算する
[[nodiscard]] std::uint64_t fingerprint_byte_size(const cue::WorkspaceEntryFingerprint &a_fingerprint) noexcept
{
    if (a_fingerprint.file.has_value())
    {
        return a_fingerprint.file->byteSize;
    }
    std::uint64_t total = 0U;
    for (const cue::WorkspaceManifestEntry &entry : a_fingerprint.manifest)
    {
        if (entry.file.has_value())
        {
            if (entry.file->byteSize > std::numeric_limits<std::uint64_t>::max() - total)
            {
                return std::numeric_limits<std::uint64_t>::max();
            }
            total += entry.file->byteSize;
        }
    }
    return total;
}
} // namespace

namespace cue::project_files
{
ProjectFileAccessPolicy ProjectFileAccessPolicy::editor_default() noexcept
{
    return ProjectFileAccessPolicy();
}

bool ProjectFileAccessPolicy::can_list(ProjectFileArea a_area) const noexcept
{
    return a_area == ProjectFileArea::SourceAssets;
}

bool ProjectFileAccessPolicy::can_mutate(ProjectFileArea a_area) const noexcept
{
    return a_area == ProjectFileArea::SourceAssets;
}

ProjectFileOperationResult::ProjectFileOperationResult(std::string a_operationId, ProjectFileOperationKind a_kind,
                                                       ProjectFileArea a_area, std::optional<std::string> a_source,
                                                       std::string a_destination, ProjectFileOperationStage a_stage,
                                                       ProjectFileOperationOutcome a_outcome,
                                                       std::optional<Error> a_primaryError,
                                                       std::vector<Error> a_secondaryDiagnostics,
                                                       std::vector<std::string> a_rescanDirectories) noexcept
    : m_operationId(std::move(a_operationId)), m_kind(a_kind), m_area(a_area), m_source(std::move(a_source)),
      m_destination(std::move(a_destination)), m_stage(a_stage), m_outcome(a_outcome),
      m_primaryError(std::move(a_primaryError)), m_secondaryDiagnostics(std::move(a_secondaryDiagnostics)),
      m_rescanDirectories(std::move(a_rescanDirectories))
{
}

std::string_view ProjectFileOperationResult::operation_id() const noexcept
{
    return m_operationId;
}

ProjectFileOperationKind ProjectFileOperationResult::kind() const noexcept
{
    return m_kind;
}

ProjectFileArea ProjectFileOperationResult::area() const noexcept
{
    return m_area;
}

std::optional<std::string_view> ProjectFileOperationResult::source() const noexcept
{
    return m_source.has_value() ? std::optional<std::string_view>(*m_source) : std::nullopt;
}

std::string_view ProjectFileOperationResult::destination() const noexcept
{
    return m_destination;
}

ProjectFileOperationStage ProjectFileOperationResult::stage() const noexcept
{
    return m_stage;
}

ProjectFileOperationOutcome ProjectFileOperationResult::outcome() const noexcept
{
    return m_outcome;
}

const Error *ProjectFileOperationResult::try_primary_error() const noexcept
{
    return m_primaryError.has_value() ? &*m_primaryError : nullptr;
}

std::span<const Error> ProjectFileOperationResult::secondary_diagnostics() const noexcept
{
    return m_secondaryDiagnostics;
}

std::span<const std::string> ProjectFileOperationResult::rescan_directories() const noexcept
{
    return m_rescanDirectories;
}

ProjectFileService::ProjectFileService(ProjectId a_projectId, ProjectRoots a_roots, ProjectFileAccessPolicy a_policy,
                                       std::unique_ptr<WorkspaceFilesystem> a_workspace,
                                       std::unique_ptr<ProjectFileOperationIdSource> a_operationIdSource,
                                       const AssertContext &a_assertContext) noexcept
    : m_projectId(std::move(a_projectId)), m_roots(std::move(a_roots)), m_policy(a_policy),
      m_workspace(std::move(a_workspace)), m_operationIdSource(std::move(a_operationIdSource)),
      m_assertContext(a_assertContext), m_ownerThread(std::this_thread::get_id())
{
}

Result<ProjectFileService> ProjectFileService::create(const ProjectDescriptor &a_descriptor,
                                                      std::unique_ptr<WorkspaceFilesystem> a_workspace,
                                                      std::unique_ptr<ProjectFileOperationIdSource> a_operationIdSource,
                                                      const AssertContext &a_assertContext) noexcept
{
    if (a_workspace == nullptr || a_operationIdSource == nullptr)
    {
        return Result<ProjectFileService>::failure(make_project_file_error(
            a_assertContext, ProjectFileError::InvalidRequest, "Project file service dependency is missing"));
    }
    Result<void> validated = validate_project_descriptor(a_descriptor, a_assertContext);
    if (!validated)
    {
        return Result<ProjectFileService>::failure(
            reclassify_project_file_error(a_assertContext, ProjectFileError::InvalidRequest,
                                          "Project descriptor is invalid", std::move(*validated.try_error())));
    }

    Result<RelativePath> descriptorLocator = RelativePath::parse("CueProject.json", a_assertContext);
    if (!descriptorLocator)
    {
        return Result<ProjectFileService>::failure(reclassify_project_file_error(
            a_assertContext, ProjectFileError::InvalidRequest, "Project descriptor locator is invalid",
            std::move(*descriptorLocator.try_error())));
    }
    Result<BoundWorkspacePath> boundDescriptor =
        a_workspace->bind_root_path(std::move(*descriptorLocator.try_value()), a_assertContext);
    if (!boundDescriptor)
    {
        return Result<ProjectFileService>::failure(reclassify_project_file_error(
            a_assertContext, ProjectFileError::InvalidRequest, "Project descriptor could not be bound to workspace",
            std::move(*boundDescriptor.try_error())));
    }
    Result<std::vector<std::byte>> descriptorBytes =
        a_workspace->read_file_bounded(*boundDescriptor.try_value(), k_maximumProjectDescriptorBytes);
    if (!descriptorBytes)
    {
        const ProjectFileError classification =
            classify_project_file_error(*descriptorBytes.try_error(), WorkspaceMutationOutcome::NotCommitted);
        Error cause = std::move(*descriptorBytes.try_error());
        return Result<ProjectFileService>::failure(reclassify_project_file_error(
            a_assertContext, classification, "Project descriptor could not be read from workspace", std::move(cause)));
    }
    const std::string_view descriptorText(reinterpret_cast<const char *>(descriptorBytes.try_value()->data()),
                                          descriptorBytes.try_value()->size());
    Result<ProjectDescriptor> workspaceDescriptor = parse_project_descriptor(descriptorText, a_assertContext);
    if (!workspaceDescriptor || !a_descriptor.equivalent_to(*workspaceDescriptor.try_value()))
    {
        std::optional<Error> cause;
        if (!workspaceDescriptor)
        {
            cause.emplace(std::move(*workspaceDescriptor.try_error()));
        }
        return Result<ProjectFileService>::failure(
            cause.has_value()
                ? reclassify_project_file_error(a_assertContext, ProjectFileError::InvalidRequest,
                                                "Workspace project descriptor is invalid", std::move(*cause))
                : make_project_file_error(a_assertContext, ProjectFileError::InvalidRequest,
                                          "Workspace project descriptor does not match the requested project"));
    }

    Result<ProjectId> projectId = ProjectId::parse(a_descriptor.project_id().text(), a_assertContext);
    Result<RelativePath> sourceAssets =
        RelativePath::parse(a_descriptor.roots().source_assets().text(), a_assertContext);
    Result<RelativePath> runtimeAssets =
        RelativePath::parse(a_descriptor.roots().runtime_assets().text(), a_assertContext);
    Result<RelativePath> generated = RelativePath::parse(a_descriptor.roots().generated().text(), a_assertContext);
    Result<RelativePath> saved = RelativePath::parse(a_descriptor.roots().saved().text(), a_assertContext);
    if (!projectId || !sourceAssets || !runtimeAssets || !generated || !saved)
    {
        return Result<ProjectFileService>::failure(make_project_file_error(
            a_assertContext, ProjectFileError::InvalidRequest, "Project descriptor snapshot could not be cloned"));
    }

    ProjectRoots roots(std::move(*sourceAssets.try_value()), std::move(*runtimeAssets.try_value()),
                       std::move(*generated.try_value()), std::move(*saved.try_value()));
    Result<WorkspaceDirectory> sourceRoot = a_workspace->bind_directory(roots.source_assets(), a_assertContext);
    if (!sourceRoot)
    {
        const ProjectFileError classification =
            classify_project_file_error(*sourceRoot.try_error(), WorkspaceMutationOutcome::NotCommitted);
        Error cause = std::move(*sourceRoot.try_error());
        return Result<ProjectFileService>::failure(reclassify_project_file_error(
            a_assertContext, classification, "Project source assets root could not be bound", std::move(cause)));
    }
    Result<void> verifiedSourceRoot = a_workspace->verify_directory(*sourceRoot.try_value());
    if (!verifiedSourceRoot)
    {
        const ProjectFileError classification =
            classify_project_file_error(*verifiedSourceRoot.try_error(), WorkspaceMutationOutcome::NotCommitted);
        Error cause = std::move(*verifiedSourceRoot.try_error());
        return Result<ProjectFileService>::failure(reclassify_project_file_error(
            a_assertContext, classification, "Project source assets root is unavailable", std::move(cause)));
    }

    ProjectFileService service(std::move(*projectId.try_value()), std::move(roots),
                               ProjectFileAccessPolicy::editor_default(), std::move(a_workspace),
                               std::move(a_operationIdSource), a_assertContext);
    return Result<ProjectFileService>::success(std::move(service));
}

const ProjectId &ProjectFileService::project_id() const noexcept
{
    return m_projectId;
}

const ProjectRoots &ProjectFileService::roots() const noexcept
{
    return m_roots;
}

const ProjectFileAccessPolicy &ProjectFileService::access_policy() const noexcept
{
    return m_policy;
}

std::span<const RecoveryEntry> ProjectFileService::recovery_entries() const noexcept
{
    return m_recoveryEntries;
}

std::span<const Error> ProjectFileService::recovery_diagnostics() const noexcept
{
    return m_recoveryDiagnostics;
}

Result<void> ProjectFileService::ensure_trash_root(std::string_view a_operationId) noexcept
{
    constexpr std::array<std::string_view, 2U> k_directories{"Editor", "Editor/Trash"};
    for (const std::string_view text : k_directories)
    {
        Result<RelativePath> relative = RelativePath::parse(text, m_assertContext);
        if (!relative)
        {
            return Result<void>::failure(std::move(*relative.try_error()));
        }
        Result<BoundWorkspacePath> bound =
            m_workspace->bind_path(m_roots.saved(), std::move(*relative.try_value()), m_assertContext);
        if (!bound)
        {
            return Result<void>::failure(std::move(*bound.try_error()));
        }
        WorkspaceMutationResult created = m_workspace->create_directory_new(*bound.try_value(), a_operationId);
        const bool alreadyExists =
            created.primaryError.has_value() && is_io_error(*created.primaryError, IoError::AlreadyExists);
        if (created.outcome == WorkspaceMutationOutcome::NotCommitted && !alreadyExists)
        {
            return Result<void>::failure(created.primaryError.has_value()
                                             ? std::move(*created.primaryError)
                                             : make_project_file_error(m_assertContext,
                                                                       ProjectFileError::StorageFailure,
                                                                       "Project trash directory creation failed"));
        }
        WorkspaceDirectory directory = WorkspaceDirectory::from_bound_path(std::move(*bound.try_value()));
        Result<void> verified = m_workspace->verify_directory(directory);
        if (!verified)
        {
            return Result<void>::failure(std::move(*verified.try_error()));
        }
    }
    return Result<void>::success();
}

Result<std::vector<std::byte>> ProjectFileService::read_trash_record_bytes(std::string_view a_operationId) noexcept
{
    std::string path;
    try
    {
        path = "Editor/Trash/";
        path.append(a_operationId);
        path.append("/Record.cuetrash");
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    Result<RelativePath> relative = RelativePath::parse(path, m_assertContext);
    if (!relative)
    {
        return Result<std::vector<std::byte>>::failure(std::move(*relative.try_error()));
    }
    Result<BoundWorkspacePath> bound =
        m_workspace->bind_path(m_roots.saved(), std::move(*relative.try_value()), m_assertContext);
    if (!bound)
    {
        return Result<std::vector<std::byte>>::failure(std::move(*bound.try_error()));
    }
    return m_workspace->read_file_bounded(*bound.try_value(), k_maximumTrashRecordBytes);
}

Result<ProjectFileOperationResult> ProjectFileService::create_directory(ProjectFileArea a_area,
                                                                        RelativePath a_destination) noexcept
{
    return execute_create(ProjectFileOperationKind::DirectoryCreation, a_area, std::move(a_destination), {});
}

Result<ProjectFileOperationResult> ProjectFileService::create_file(ProjectFileArea a_area, RelativePath a_destination,
                                                                   std::span<const std::byte> a_bytes) noexcept
{
    return execute_create(ProjectFileOperationKind::FileCreation, a_area, std::move(a_destination), a_bytes);
}

Result<ProjectFileOperationResult> ProjectFileService::rename(ProjectFileArea a_area, RelativePath a_source,
                                                              RelativePath a_destination,
                                                              TraversalLimits a_limits) noexcept
{
    return execute_transfer(ProjectFileOperationKind::Rename, a_area, std::move(a_source), std::move(a_destination),
                            a_limits, ContentVerificationLimits{1U, 1U});
}

Result<ProjectFileOperationResult> ProjectFileService::move(ProjectFileArea a_area, RelativePath a_source,
                                                            RelativePath a_destination,
                                                            TraversalLimits a_limits) noexcept
{
    return execute_transfer(ProjectFileOperationKind::Move, a_area, std::move(a_source), std::move(a_destination),
                            a_limits, ContentVerificationLimits{1U, 1U});
}

Result<ProjectFileOperationResult> ProjectFileService::copy(ProjectFileArea a_area, RelativePath a_source,
                                                            RelativePath a_destination,
                                                            TraversalLimits a_traversalLimits,
                                                            ContentVerificationLimits a_contentLimits) noexcept
{
    return execute_transfer(ProjectFileOperationKind::Copy, a_area, std::move(a_source), std::move(a_destination),
                            a_traversalLimits, a_contentLimits);
}

Result<ProjectFileOperationResult> ProjectFileService::delete_entry(ProjectFileArea a_area, RelativePath a_source,
                                                                    TraversalLimits a_traversalLimits,
                                                                    ContentVerificationLimits a_contentLimits) noexcept
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Result<ProjectFileOperationResult>::failure(make_project_file_error(
            m_assertContext, ProjectFileError::InvalidRequest, "Project file service was called from another thread"));
    }
    if (m_isBusy)
    {
        return Result<ProjectFileOperationResult>::failure(make_project_file_error(
            m_assertContext, ProjectFileError::Busy, "Project file mutation is already active"));
    }
    BusyReset busy(m_isBusy);

    Result<std::string> generatedId = m_operationIdSource->next_operation_id();
    if (!generatedId)
    {
        return Result<ProjectFileOperationResult>::failure(reclassify_project_file_error(
            m_assertContext, ProjectFileError::InvalidRequest, "Project file operation id generation failed",
            std::move(*generatedId.try_error())));
    }
    Result<ProjectId> validatedId = ProjectId::parse(*generatedId.try_value(), m_assertContext);
    if (!validatedId)
    {
        return Result<ProjectFileOperationResult>::failure(
            reclassify_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                          "Project file operation id is invalid", std::move(*validatedId.try_error())));
    }
    std::string operationId = std::move(*generatedId.try_value());
    std::string source;
    try
    {
        source.assign(a_source.text());
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }

    /// @brief Delete経路の共通Metadataを保持したOperation Resultを構築する
    const auto makeResult = [&](ProjectFileOperationStage a_stage, ProjectFileOperationOutcome a_outcome,
                                std::optional<Error> a_primary,
                                std::vector<Error> a_secondary) noexcept -> Result<ProjectFileOperationResult>
    {
        std::vector<std::string> rescan;
        try
        {
            rescan.push_back(parent_locator(source, m_assertContext));
        }
        catch (...)
        {
            terminate_allocation(m_assertContext);
        }
        ProjectFileOperationResult result(operationId, ProjectFileOperationKind::RecoverableDelete, a_area, source,
                                          operationId, a_stage, a_outcome, std::move(a_primary), std::move(a_secondary),
                                          std::move(rescan));
        return Result<ProjectFileOperationResult>::success(std::move(result));
    };
    /// @brief Delete失敗CauseをProject File Errorへ再分類してOperation Resultへ保持する
    const auto fail = [&](ProjectFileOperationStage a_stage, ProjectFileError a_code, std::string_view a_message,
                          Error a_cause, ProjectFileOperationOutcome a_outcome,
                          std::vector<Error> a_secondary = {}) noexcept -> Result<ProjectFileOperationResult>
    {
        std::optional<Error> primary(
            reclassify_project_file_error(m_assertContext, a_code, a_message, std::move(a_cause)));
        return makeResult(a_stage, a_outcome, std::move(primary), std::move(a_secondary));
    };

    if (!m_policy.can_mutate(a_area) || !a_traversalLimits.is_valid() || !a_contentLimits.is_valid())
    {
        std::optional<Error> primary(make_project_file_error(
            m_assertContext,
            !m_policy.can_mutate(a_area) ? ProjectFileError::ProtectedEntry : ProjectFileError::InvalidRequest,
            !m_policy.can_mutate(a_area) ? "Project file area is protected from deletion"
                                         : "Project file delete limits are invalid"));
        return makeResult(ProjectFileOperationStage::ValidateRequest, ProjectFileOperationOutcome::NotCommitted,
                          std::move(primary), {});
    }
    Result<BoundWorkspacePath> boundSource = m_workspace->bind_path(area_root(a_area), a_source, m_assertContext);
    if (!boundSource)
    {
        const ProjectFileError classification =
            classify_project_file_error(*boundSource.try_error(), WorkspaceMutationOutcome::NotCommitted);
        Error cause = std::move(*boundSource.try_error());
        return fail(ProjectFileOperationStage::BindSource, classification, "Project file delete source binding failed",
                    std::move(cause), ProjectFileOperationOutcome::NotCommitted);
    }
    Result<WorkspaceEntryFingerprint> fingerprint =
        m_workspace->fingerprint_entry(*boundSource.try_value(), a_traversalLimits, a_contentLimits);
    if (!fingerprint)
    {
        const ProjectFileError classification =
            classify_project_file_error(*fingerprint.try_error(), WorkspaceMutationOutcome::NotCommitted);
        Error cause = std::move(*fingerprint.try_error());
        return fail(ProjectFileOperationStage::ValidateRequest, classification,
                    "Project file delete source verification failed", std::move(cause),
                    ProjectFileOperationOutcome::NotCommitted);
    }
    Result<void> trashRoot = ensure_trash_root(operationId);
    if (!trashRoot)
    {
        return fail(ProjectFileOperationStage::PrepareRecovery, ProjectFileError::StorageFailure,
                    "Project trash root preparation failed", std::move(*trashRoot.try_error()),
                    ProjectFileOperationOutcome::NotCommitted);
    }

    project_files_private::TrashRecord record;
    try
    {
        record.projectId.assign(m_projectId.text());
        record.operationId = operationId;
        record.originalPath = source;
        record.fingerprint = *fingerprint.try_value();
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    /// @brief 同じTrash Recordを指定状態のCanonical Byte列へ変換する
    const auto serializeState =
        [&](project_files_private::TrashRecordState a_state) noexcept -> Result<std::vector<std::byte>>
    {
        record.state = a_state;
        return project_files_private::serialize_trash_record(record, m_assertContext);
    };
    Result<std::vector<std::byte>> allocating = serializeState(project_files_private::TrashRecordState::Allocating);
    Result<std::vector<std::byte>> prepared = serializeState(project_files_private::TrashRecordState::Prepared);
    Result<std::vector<std::byte>> trashed = serializeState(project_files_private::TrashRecordState::Trashed);
    if (!allocating || !prepared || !trashed)
    {
        Error cause = !allocating ? std::move(*allocating.try_error())
                      : !prepared ? std::move(*prepared.try_error())
                                  : std::move(*trashed.try_error());
        return fail(ProjectFileOperationStage::PrepareRecovery, ProjectFileError::RecoveryRequired,
                    "Project trash record serialization failed", std::move(cause),
                    ProjectFileOperationOutcome::NotCommitted);
    }

    /// @brief Saved Root内の内部Recovery PathをCapabilityへ変換する
    const auto bindSaved = [&](std::string_view a_path) noexcept -> Result<BoundWorkspacePath>
    {
        Result<RelativePath> relative = RelativePath::parse(a_path, m_assertContext);
        if (!relative)
        {
            return Result<BoundWorkspacePath>::failure(std::move(*relative.try_error()));
        }
        return m_workspace->bind_path(m_roots.saved(), std::move(*relative.try_value()), m_assertContext);
    };
    std::string operationPath;
    std::string recordPath;
    std::string payloadPath;
    try
    {
        operationPath = "Editor/Trash/" + operationId;
        recordPath = operationPath + "/Record.cuetrash";
        payloadPath = operationPath + "/Payload";
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    Result<BoundWorkspacePath> trashParent = bindSaved("Editor/Trash");
    Result<BoundWorkspacePath> staging =
        trashParent ? m_workspace->bind_operation_staging_path(*trashParent.try_value(), operationId, "cuetrash",
                                                               m_assertContext)
                    : Result<BoundWorkspacePath>::failure(std::move(*trashParent.try_error()));
    Result<BoundWorkspacePath> operation = bindSaved(operationPath);
    Result<RelativePath> recordChild = RelativePath::parse("Record.cuetrash", m_assertContext);
    Result<BoundWorkspacePath> stagingRecord =
        staging && recordChild
            ? m_workspace->bind_child_path(*staging.try_value(), std::move(*recordChild.try_value()), m_assertContext)
            : Result<BoundWorkspacePath>::failure(
                  !staging ? make_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                                     "Project trash staging path is unavailable")
                           : std::move(*recordChild.try_error()));
    Result<BoundWorkspacePath> finalRecord = bindSaved(recordPath);
    Result<BoundWorkspacePath> payload = bindSaved(payloadPath);
    if (!staging || !operation || !stagingRecord || !finalRecord || !payload)
    {
        Error cause = !staging         ? std::move(*staging.try_error())
                      : !operation     ? std::move(*operation.try_error())
                      : !stagingRecord ? std::move(*stagingRecord.try_error())
                      : !finalRecord   ? std::move(*finalRecord.try_error())
                                       : std::move(*payload.try_error());
        return fail(ProjectFileOperationStage::PrepareRecovery, ProjectFileError::InvalidRequest,
                    "Project trash path binding failed", std::move(cause), ProjectFileOperationOutcome::NotCommitted);
    }

    /// @brief 下位MutationのSecondary DiagnosticsをProject Operationへ移送する
    const auto appendMutationDiagnostics =
        [&](WorkspaceMutationResult &a_mutation, std::vector<Error> &a_secondary) noexcept
    {
        try
        {
            for (Error &diagnostic : a_mutation.secondaryDiagnostics)
            {
                a_secondary.push_back(std::move(diagnostic));
            }
        }
        catch (...)
        {
            terminate_allocation(m_assertContext);
        }
    };
    /// @brief Payload公開前のRecordと空ContainerだけをCleanupして診断を保持する
    const auto cleanupContainer = [&](const BoundWorkspacePath &a_recordPath, const BoundWorkspacePath &a_directoryPath,
                                      std::vector<Error> &a_secondary) noexcept
    {
        WorkspaceMutationResult removedRecord = m_workspace->remove_file_or_empty_directory(a_recordPath);
        appendMutationDiagnostics(removedRecord, a_secondary);
        if (removedRecord.primaryError.has_value())
        {
            try
            {
                a_secondary.push_back(std::move(*removedRecord.primaryError));
            }
            catch (...)
            {
                terminate_allocation(m_assertContext);
            }
        }
        if (removedRecord.outcome == WorkspaceMutationOutcome::NotCommitted ||
            removedRecord.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
        {
            return;
        }
        WorkspaceMutationResult removedDirectory = m_workspace->remove_file_or_empty_directory(a_directoryPath);
        appendMutationDiagnostics(removedDirectory, a_secondary);
        if (removedDirectory.primaryError.has_value())
        {
            try
            {
                a_secondary.push_back(std::move(*removedDirectory.primaryError));
            }
            catch (...)
            {
                terminate_allocation(m_assertContext);
            }
        }
    };

    WorkspaceMutationResult createdDirectory = m_workspace->create_directory_new(*staging.try_value(), operationId);
    if (createdDirectory.outcome == WorkspaceMutationOutcome::NotCommitted ||
        createdDirectory.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        Error cause = createdDirectory.primaryError.has_value()
                          ? std::move(*createdDirectory.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::StorageFailure,
                                                    "Project trash staging creation failed");
        const ProjectFileError classification = classify_project_file_error(cause, createdDirectory.outcome);
        return fail(ProjectFileOperationStage::PrepareRecovery, classification, "Project trash staging creation failed",
                    std::move(cause), convert_outcome(createdDirectory.outcome),
                    std::move(createdDirectory.secondaryDiagnostics));
    }
    WorkspaceMutationResult wroteAllocating =
        m_workspace->create_file_new_atomic(*stagingRecord.try_value(), *allocating.try_value(), operationId);
    if (wroteAllocating.outcome == WorkspaceMutationOutcome::NotCommitted ||
        wroteAllocating.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        std::vector<Error> secondary = std::move(wroteAllocating.secondaryDiagnostics);
        cleanupContainer(*stagingRecord.try_value(), *staging.try_value(), secondary);
        Error cause = wroteAllocating.primaryError.has_value()
                          ? std::move(*wroteAllocating.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::StorageFailure,
                                                    "Project trash allocating record write failed");
        const ProjectFileError classification = classify_project_file_error(cause, wroteAllocating.outcome);
        return fail(ProjectFileOperationStage::PrepareRecovery, classification,
                    "Project trash allocating record write failed", std::move(cause),
                    convert_outcome(wroteAllocating.outcome), std::move(secondary));
    }
    WorkspaceMutationResult publishedContainer =
        m_workspace->rename_entry(*staging.try_value(), *operation.try_value(), a_traversalLimits);
    if (publishedContainer.outcome == WorkspaceMutationOutcome::NotCommitted ||
        publishedContainer.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        Error cause = publishedContainer.primaryError.has_value()
                          ? std::move(*publishedContainer.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project trash container publish requires reconciliation");
        const ProjectFileError classification = classify_project_file_error(cause, publishedContainer.outcome);
        return fail(ProjectFileOperationStage::PrepareRecovery, classification,
                    "Project trash container publish failed", std::move(cause),
                    convert_outcome(publishedContainer.outcome), std::move(publishedContainer.secondaryDiagnostics));
    }
    WorkspaceMutationResult wrotePrepared =
        m_workspace->replace_file_atomic(*finalRecord.try_value(), *prepared.try_value(), operationId);
    if (wrotePrepared.outcome == WorkspaceMutationOutcome::NotCommitted ||
        wrotePrepared.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        std::vector<Error> secondary = std::move(wrotePrepared.secondaryDiagnostics);
        cleanupContainer(*finalRecord.try_value(), *operation.try_value(), secondary);
        Error cause = wrotePrepared.primaryError.has_value()
                          ? std::move(*wrotePrepared.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project trash prepared record write failed");
        const ProjectFileError classification = classify_project_file_error(cause, wrotePrepared.outcome);
        return fail(ProjectFileOperationStage::UpdateRecoveryRecord, classification,
                    "Project trash prepared record write failed", std::move(cause),
                    convert_outcome(wrotePrepared.outcome), std::move(secondary));
    }

    Result<WorkspaceEntryFingerprint> current =
        m_workspace->fingerprint_entry(*boundSource.try_value(), a_traversalLimits, a_contentLimits);
    if (!current || *current.try_value() != *fingerprint.try_value())
    {
        std::vector<Error> secondary;
        cleanupContainer(*finalRecord.try_value(), *operation.try_value(), secondary);
        Error cause = !current ? std::move(*current.try_error())
                               : make_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                                         "Project file delete source changed before publish");
        return fail(ProjectFileOperationStage::Verify, ProjectFileError::InvalidRequest,
                    "Project file delete source verification failed", std::move(cause),
                    ProjectFileOperationOutcome::NotCommitted, std::move(secondary));
    }
    WorkspaceMutationResult moved = m_workspace->rename_entry_if_matches(
        *boundSource.try_value(), *payload.try_value(), *fingerprint.try_value(), a_traversalLimits, a_contentLimits);
    if (moved.outcome == WorkspaceMutationOutcome::NotCommitted)
    {
        std::vector<Error> secondary = std::move(moved.secondaryDiagnostics);
        cleanupContainer(*finalRecord.try_value(), *operation.try_value(), secondary);
        Error cause = moved.primaryError.has_value()
                          ? std::move(*moved.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::StorageFailure,
                                                    "Project file delete rename failed");
        const ProjectFileError classification = classify_project_file_error(cause, moved.outcome);
        return fail(ProjectFileOperationStage::NativePublish, classification, "Project file delete rename failed",
                    std::move(cause), ProjectFileOperationOutcome::NotCommitted, std::move(secondary));
    }
    if (moved.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        Error cause = moved.primaryError.has_value()
                          ? std::move(*moved.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project file delete rename requires reconciliation");
        return fail(ProjectFileOperationStage::Verify, ProjectFileError::RecoveryRequired,
                    "Project file delete rename requires reconciliation", std::move(cause),
                    ProjectFileOperationOutcome::ReconciliationRequired, std::move(moved.secondaryDiagnostics));
    }
    WorkspaceMutationResult wroteTrashed =
        m_workspace->replace_file_atomic(*finalRecord.try_value(), *trashed.try_value(), operationId);
    if (wroteTrashed.outcome == WorkspaceMutationOutcome::NotCommitted ||
        wroteTrashed.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        Error cause = wroteTrashed.primaryError.has_value()
                          ? std::move(*wroteTrashed.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project trash final record write requires reconciliation");
        return fail(ProjectFileOperationStage::UpdateRecoveryRecord, ProjectFileError::RecoveryRequired,
                    "Project trash final record write failed", std::move(cause),
                    ProjectFileOperationOutcome::ReconciliationRequired, std::move(wroteTrashed.secondaryDiagnostics));
    }

    RecoveryEntry recovery;
    try
    {
        recovery.operationId = operationId;
        recovery.originalPath = source;
        recovery.originalArea = a_area;
        recovery.entryType = fingerprint.try_value()->type;
        recovery.byteSize = fingerprint_byte_size(*fingerprint.try_value());
        recovery.descendantCount = fingerprint.try_value()->manifest.size();
        recovery.state = RecoveryEntryState::Recoverable;
        m_recoveryEntries.push_back(std::move(recovery));
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    std::optional<Error> primary(
        make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                "Project file delete is visible but one or more durability barriers are unknown"));
    return makeResult(ProjectFileOperationStage::Verify, ProjectFileOperationOutcome::CommittedButDurabilityUnknown,
                      std::move(primary), {});
}

Result<ProjectFileOperationResult> ProjectFileService::restore(std::string_view a_operationId,
                                                               TraversalLimits a_traversalLimits,
                                                               ContentVerificationLimits a_contentLimits) noexcept
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Result<ProjectFileOperationResult>::failure(make_project_file_error(
            m_assertContext, ProjectFileError::InvalidRequest, "Project file service was called from another thread"));
    }
    if (m_isBusy)
    {
        return Result<ProjectFileOperationResult>::failure(make_project_file_error(
            m_assertContext, ProjectFileError::Busy, "Project file mutation is already active"));
    }
    BusyReset busy(m_isBusy);
    Result<ProjectId> validatedId = ProjectId::parse(a_operationId, m_assertContext);
    if (!validatedId || !a_traversalLimits.is_valid() || !a_contentLimits.is_valid())
    {
        return Result<ProjectFileOperationResult>::failure(
            !validatedId ? reclassify_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                                         "Project restore operation id is invalid",
                                                         std::move(*validatedId.try_error()))
                         : make_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                                   "Project restore limits are invalid"));
    }
    std::string operationId;
    try
    {
        operationId.assign(a_operationId);
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }

    Result<std::vector<std::byte>> recordBytes = read_trash_record_bytes(operationId);
    if (!recordBytes)
    {
        return Result<ProjectFileOperationResult>::failure(reclassify_project_file_error(
            m_assertContext, ProjectFileError::RecoveryRequired, "Project trash record could not be read",
            std::move(*recordBytes.try_error())));
    }
    Result<project_files_private::TrashRecord> record = project_files_private::parse_trash_record(
        *recordBytes.try_value(), project_files_private::trash_record_hard_limits(), m_assertContext);
    if (!record || record.try_value()->projectId != m_projectId.text() ||
        record.try_value()->operationId != operationId ||
        record.try_value()->state != project_files_private::TrashRecordState::Trashed)
    {
        return Result<ProjectFileOperationResult>::failure(
            !record ? std::move(*record.try_error())
                    : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                              "Project trash record is not a recoverable entry"));
    }
    Result<RelativePath> original = RelativePath::parse(record.try_value()->originalPath, m_assertContext);
    if (!original)
    {
        return Result<ProjectFileOperationResult>::failure(std::move(*original.try_error()));
    }
    std::string destination = record.try_value()->originalPath;

    /// @brief Restore経路の共通Metadataを保持したOperation Resultを構築する
    const auto makeResult = [&](ProjectFileOperationStage a_stage, ProjectFileOperationOutcome a_outcome,
                                std::optional<Error> a_primary,
                                std::vector<Error> a_secondary) noexcept -> Result<ProjectFileOperationResult>
    {
        std::vector<std::string> rescan;
        try
        {
            rescan.push_back(parent_locator(destination, m_assertContext));
        }
        catch (...)
        {
            terminate_allocation(m_assertContext);
        }
        ProjectFileOperationResult result(operationId, ProjectFileOperationKind::Restore, ProjectFileArea::SourceAssets,
                                          operationId, destination, a_stage, a_outcome, std::move(a_primary),
                                          std::move(a_secondary), std::move(rescan));
        return Result<ProjectFileOperationResult>::success(std::move(result));
    };
    /// @brief Restore失敗CauseをProject File Errorへ再分類してOperation Resultへ保持する
    const auto fail = [&](ProjectFileOperationStage a_stage, ProjectFileError a_code, std::string_view a_message,
                          Error a_cause, ProjectFileOperationOutcome a_outcome,
                          std::vector<Error> a_secondary = {}) noexcept -> Result<ProjectFileOperationResult>
    {
        std::optional<Error> primary(
            reclassify_project_file_error(m_assertContext, a_code, a_message, std::move(a_cause)));
        return makeResult(a_stage, a_outcome, std::move(primary), std::move(a_secondary));
    };
    /// @brief Saved Root内のRecovery Entry PathをCapabilityへ変換する
    const auto bindSaved = [&](std::string_view a_path) noexcept -> Result<BoundWorkspacePath>
    {
        Result<RelativePath> relative = RelativePath::parse(a_path, m_assertContext);
        if (!relative)
        {
            return Result<BoundWorkspacePath>::failure(std::move(*relative.try_error()));
        }
        return m_workspace->bind_path(m_roots.saved(), std::move(*relative.try_value()), m_assertContext);
    };

    std::string operationPath;
    std::string recordPath;
    std::string payloadPath;
    try
    {
        operationPath = "Editor/Trash/" + operationId;
        recordPath = operationPath + "/Record.cuetrash";
        payloadPath = operationPath + "/Payload";
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    Result<BoundWorkspacePath> boundRecord = bindSaved(recordPath);
    Result<BoundWorkspacePath> payload = bindSaved(payloadPath);
    Result<BoundWorkspacePath> operation = bindSaved(operationPath);
    Result<BoundWorkspacePath> boundDestination =
        m_workspace->bind_path(m_roots.source_assets(), std::move(*original.try_value()), m_assertContext);
    if (!boundRecord || !payload || !operation || !boundDestination)
    {
        Error cause = !boundRecord ? std::move(*boundRecord.try_error())
                      : !payload   ? std::move(*payload.try_error())
                      : !operation ? std::move(*operation.try_error())
                                   : std::move(*boundDestination.try_error());
        return fail(ProjectFileOperationStage::BindDestination, ProjectFileError::RecoveryRequired,
                    "Project restore path binding failed", std::move(cause), ProjectFileOperationOutcome::NotCommitted);
    }
    Result<WorkspaceEntryFingerprint> payloadFingerprint =
        m_workspace->fingerprint_entry(*payload.try_value(), a_traversalLimits, a_contentLimits);
    if (!payloadFingerprint || *payloadFingerprint.try_value() != record.try_value()->fingerprint)
    {
        Error cause = !payloadFingerprint
                          ? std::move(*payloadFingerprint.try_error())
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project trash payload fingerprint does not match its record");
        return fail(ProjectFileOperationStage::Verify, ProjectFileError::RecoveryRequired,
                    "Project trash payload verification failed", std::move(cause),
                    ProjectFileOperationOutcome::NotCommitted);
    }

    record.try_value()->state = project_files_private::TrashRecordState::Restoring;
    Result<std::vector<std::byte>> restoring =
        project_files_private::serialize_trash_record(*record.try_value(), m_assertContext);
    record.try_value()->state = project_files_private::TrashRecordState::Restored;
    Result<std::vector<std::byte>> restored =
        project_files_private::serialize_trash_record(*record.try_value(), m_assertContext);
    record.try_value()->state = project_files_private::TrashRecordState::Trashed;
    Result<std::vector<std::byte>> trashed =
        project_files_private::serialize_trash_record(*record.try_value(), m_assertContext);
    if (!restoring || !restored || !trashed)
    {
        Error cause = !restoring  ? std::move(*restoring.try_error())
                      : !restored ? std::move(*restored.try_error())
                                  : std::move(*trashed.try_error());
        return fail(ProjectFileOperationStage::PrepareRecovery, ProjectFileError::RecoveryRequired,
                    "Project restore record serialization failed", std::move(cause),
                    ProjectFileOperationOutcome::NotCommitted);
    }
    WorkspaceMutationResult wroteRestoring =
        m_workspace->replace_file_atomic(*boundRecord.try_value(), *restoring.try_value(), operationId);
    if (wroteRestoring.outcome == WorkspaceMutationOutcome::NotCommitted ||
        wroteRestoring.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        Error cause = wroteRestoring.primaryError.has_value()
                          ? std::move(*wroteRestoring.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project restore checkpoint write failed");
        return fail(ProjectFileOperationStage::UpdateRecoveryRecord, ProjectFileError::RecoveryRequired,
                    "Project restore checkpoint write failed", std::move(cause),
                    convert_outcome(wroteRestoring.outcome), std::move(wroteRestoring.secondaryDiagnostics));
    }

    WorkspaceMutationResult moved =
        m_workspace->rename_entry_if_matches(*payload.try_value(), *boundDestination.try_value(),
                                             record.try_value()->fingerprint, a_traversalLimits, a_contentLimits);
    if (moved.outcome == WorkspaceMutationOutcome::NotCommitted)
    {
        WorkspaceMutationResult reverted =
            m_workspace->replace_file_atomic(*boundRecord.try_value(), *trashed.try_value(), operationId);
        std::vector<Error> secondary = std::move(moved.secondaryDiagnostics);
        try
        {
            for (Error &diagnostic : reverted.secondaryDiagnostics)
            {
                secondary.push_back(std::move(diagnostic));
            }
        }
        catch (...)
        {
            terminate_allocation(m_assertContext);
        }
        if (reverted.primaryError.has_value())
        {
            try
            {
                secondary.push_back(std::move(*reverted.primaryError));
            }
            catch (...)
            {
                terminate_allocation(m_assertContext);
            }
        }
        Error cause = moved.primaryError.has_value()
                          ? std::move(*moved.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project restore rename failed");
        const ProjectFileError classification = is_io_error(cause, IoError::AlreadyExists) ? ProjectFileError::Conflict
                                                : is_io_error(cause, IoError::NotFound)
                                                    ? ProjectFileError::RecoveryRequired
                                                    : classify_project_file_error(cause, moved.outcome);
        return fail(ProjectFileOperationStage::NativePublish, classification,
                    "Project restore destination is unavailable", std::move(cause),
                    reverted.outcome == WorkspaceMutationOutcome::NotCommitted ||
                            reverted.outcome == WorkspaceMutationOutcome::ReconciliationRequired
                        ? ProjectFileOperationOutcome::ReconciliationRequired
                        : ProjectFileOperationOutcome::NotCommitted,
                    std::move(secondary));
    }
    if (moved.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        Error cause = moved.primaryError.has_value()
                          ? std::move(*moved.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project restore rename requires reconciliation");
        return fail(ProjectFileOperationStage::Verify, ProjectFileError::RecoveryRequired,
                    "Project restore rename requires reconciliation", std::move(cause),
                    ProjectFileOperationOutcome::ReconciliationRequired, std::move(moved.secondaryDiagnostics));
    }
    WorkspaceMutationResult wroteRestored =
        m_workspace->replace_file_atomic(*boundRecord.try_value(), *restored.try_value(), operationId);
    if (wroteRestored.outcome == WorkspaceMutationOutcome::NotCommitted ||
        wroteRestored.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
    {
        Error cause = wroteRestored.primaryError.has_value()
                          ? std::move(*wroteRestored.primaryError)
                          : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                    "Project restore final record write requires reconciliation");
        return fail(ProjectFileOperationStage::UpdateRecoveryRecord, ProjectFileError::RecoveryRequired,
                    "Project restore final record write failed", std::move(cause),
                    ProjectFileOperationOutcome::ReconciliationRequired, std::move(wroteRestored.secondaryDiagnostics));
    }

    std::vector<Error> cleanupDiagnostics;
    WorkspaceMutationResult removedRecord = m_workspace->remove_file_or_empty_directory(*boundRecord.try_value());
    try
    {
        for (Error &diagnostic : removedRecord.secondaryDiagnostics)
        {
            cleanupDiagnostics.push_back(std::move(diagnostic));
        }
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    if (removedRecord.primaryError.has_value())
    {
        try
        {
            cleanupDiagnostics.push_back(std::move(*removedRecord.primaryError));
        }
        catch (...)
        {
            terminate_allocation(m_assertContext);
        }
    }
    if (removedRecord.outcome != WorkspaceMutationOutcome::NotCommitted &&
        removedRecord.outcome != WorkspaceMutationOutcome::ReconciliationRequired)
    {
        WorkspaceMutationResult removedOperation = m_workspace->remove_file_or_empty_directory(*operation.try_value());
        try
        {
            for (Error &diagnostic : removedOperation.secondaryDiagnostics)
            {
                cleanupDiagnostics.push_back(std::move(diagnostic));
            }
        }
        catch (...)
        {
            terminate_allocation(m_assertContext);
        }
        if (removedOperation.primaryError.has_value())
        {
            try
            {
                cleanupDiagnostics.push_back(std::move(*removedOperation.primaryError));
            }
            catch (...)
            {
                terminate_allocation(m_assertContext);
            }
        }
    }
    std::erase_if(m_recoveryEntries,
                  /// @brief Restore済みOperationを現在のCatalogから除去する
                  [&](const RecoveryEntry &a_entry) noexcept { return a_entry.operationId == operationId; });
    std::optional<Error> primary(
        make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                "Project restore is visible but one or more durability barriers are unknown"));
    return makeResult(ProjectFileOperationStage::Verify, ProjectFileOperationOutcome::CommittedButDurabilityUnknown,
                      std::move(primary), std::move(cleanupDiagnostics));
}

Result<void> ProjectFileService::refresh_recovery_catalog(TraversalLimits a_traversalLimits,
                                                          ContentVerificationLimits a_contentLimits) noexcept
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Result<void>::failure(make_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                                             "Project file service was called from another thread"));
    }
    if (m_isBusy)
    {
        return Result<void>::failure(make_project_file_error(m_assertContext, ProjectFileError::Busy,
                                                             "Project file mutation is already active"));
    }
    if (!a_traversalLimits.is_valid() || !a_contentLimits.is_valid())
    {
        return Result<void>::failure(make_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                                             "Project recovery catalog limits are invalid"));
    }
    BusyReset busy(m_isBusy);

    constexpr std::string_view k_catalogOperationId = "00000000-0000-4000-8000-000000000001";
    Result<void> trashRoot = ensure_trash_root(k_catalogOperationId);
    if (!trashRoot)
    {
        return Result<void>::failure(reclassify_project_file_error(m_assertContext, ProjectFileError::StorageFailure,
                                                                   "Project trash root preparation failed",
                                                                   std::move(*trashRoot.try_error())));
    }
    /// @brief Catalog EntryのSaved Root内PathをCapabilityへ変換する
    const auto bindSaved = [&](std::string_view a_path) noexcept -> Result<BoundWorkspacePath>
    {
        Result<RelativePath> relative = RelativePath::parse(a_path, m_assertContext);
        if (!relative)
        {
            return Result<BoundWorkspacePath>::failure(std::move(*relative.try_error()));
        }
        return m_workspace->bind_path(m_roots.saved(), std::move(*relative.try_value()), m_assertContext);
    };
    Result<BoundWorkspacePath> trashPath = bindSaved("Editor/Trash");
    if (!trashPath)
    {
        return Result<void>::failure(std::move(*trashPath.try_error()));
    }
    WorkspaceDirectory trashDirectory = WorkspaceDirectory::from_bound_path(*trashPath.try_value());
    Result<DirectorySnapshot> snapshot = m_workspace->list_directory(trashDirectory, a_traversalLimits);
    if (!snapshot)
    {
        return Result<void>::failure(reclassify_project_file_error(m_assertContext, ProjectFileError::StorageFailure,
                                                                   "Project trash catalog enumeration failed",
                                                                   std::move(*snapshot.try_error())));
    }

    std::vector<RecoveryEntry> entries;
    std::vector<Error> diagnostics;
    /// @brief Catalog走査を継続しながらRecovery診断を保持する
    const auto addDiagnostic = [&](std::string_view a_message, std::optional<Error> a_cause = std::nullopt) noexcept
    {
        try
        {
            diagnostics.push_back(
                a_cause.has_value()
                    ? reclassify_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired, a_message,
                                                    std::move(*a_cause))
                    : make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired, a_message));
        }
        catch (...)
        {
            terminate_allocation(m_assertContext);
        }
    };
    if (snapshot.try_value()->state != WorkspaceSnapshotState::Complete)
    {
        addDiagnostic("Project trash catalog enumeration requires a rescan");
    }

    for (const WorkspaceEntry &container : snapshot.try_value()->entries)
    {
        constexpr std::string_view k_stagingPrefix = ".";
        constexpr std::string_view k_stagingSuffix = ".cuetrash-staging";
        if (container.displayName.starts_with(k_stagingPrefix) && container.displayName.ends_with(k_stagingSuffix))
        {
            if (container.displayName.size() != k_stagingPrefix.size() + 36U + k_stagingSuffix.size())
            {
                addDiagnostic("Project trash contains a malformed staging container name");
                continue;
            }
            const std::size_t idLength = container.displayName.size() - k_stagingPrefix.size() - k_stagingSuffix.size();
            const std::string_view stagingId =
                std::string_view(container.displayName).substr(k_stagingPrefix.size(), idLength);
            Result<ProjectId> validatedStagingId = ProjectId::parse(stagingId, m_assertContext);
            Result<BoundWorkspacePath> staging =
                validatedStagingId ? m_workspace->bind_operation_staging_path(*trashPath.try_value(), stagingId,
                                                                              "cuetrash", m_assertContext)
                                   : Result<BoundWorkspacePath>::failure(
                                         make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                                 "Project trash staging operation id is invalid"));
            if (!staging)
            {
                addDiagnostic("Project trash contains an unrecognized staging container",
                              std::move(*staging.try_error()));
                continue;
            }
            WorkspaceDirectory stagingDirectory = WorkspaceDirectory::from_bound_path(*staging.try_value());
            Result<DirectorySnapshot> stagingSnapshot =
                m_workspace->list_directory(stagingDirectory, a_traversalLimits);
            if (!stagingSnapshot || stagingSnapshot.try_value()->state != WorkspaceSnapshotState::Complete ||
                !stagingSnapshot.try_value()->entries.empty())
            {
                if (!stagingSnapshot)
                {
                    addDiagnostic("Project trash staging container could not be verified",
                                  std::move(*stagingSnapshot.try_error()));
                }
                else
                {
                    addDiagnostic("Project trash staging container is not an empty operation-owned directory");
                }
                continue;
            }
            WorkspaceMutationResult removedStaging = m_workspace->remove_file_or_empty_directory(*staging.try_value());
            if (removedStaging.primaryError.has_value())
            {
                addDiagnostic("Project trash empty staging cleanup was not durable",
                              std::move(*removedStaging.primaryError));
            }
            for (Error &diagnostic : removedStaging.secondaryDiagnostics)
            {
                addDiagnostic("Project trash empty staging cleanup reported a secondary failure",
                              std::move(diagnostic));
            }
            if (removedStaging.outcome == WorkspaceMutationOutcome::NotCommitted ||
                removedStaging.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
            {
                addDiagnostic("Project trash empty staging container could not be removed");
            }
            continue;
        }

        Result<ProjectId> operationId = ProjectId::parse(container.displayName, m_assertContext);
        if (container.type != WorkspaceEntryType::Directory || !container.locator.has_value() || !operationId)
        {
            addDiagnostic("Project trash contains an unrecognized operation container");
            continue;
        }
        std::string operationPath;
        std::string recordPath;
        std::string payloadPath;
        try
        {
            operationPath = "Editor/Trash/" + container.displayName;
            recordPath = operationPath + "/Record.cuetrash";
            payloadPath = operationPath + "/Payload";
        }
        catch (...)
        {
            terminate_allocation(m_assertContext);
        }
        Result<BoundWorkspacePath> boundRecord = bindSaved(recordPath);
        Result<BoundWorkspacePath> payload = bindSaved(payloadPath);
        Result<BoundWorkspacePath> operation = bindSaved(operationPath);
        if (!boundRecord || !payload || !operation)
        {
            Error cause = !boundRecord ? std::move(*boundRecord.try_error())
                          : !payload   ? std::move(*payload.try_error())
                                       : std::move(*operation.try_error());
            addDiagnostic("Project trash operation paths are invalid", std::move(cause));
            continue;
        }
        WorkspaceDirectory operationDirectory = WorkspaceDirectory::from_bound_path(*operation.try_value());
        Result<DirectorySnapshot> operationSnapshot =
            m_workspace->list_directory(operationDirectory, a_traversalLimits);
        if (!operationSnapshot)
        {
            addDiagnostic("Project trash operation could not be enumerated", std::move(*operationSnapshot.try_error()));
            continue;
        }
        const WorkspaceEntry *recordEntry = nullptr;
        const WorkspaceEntry *payloadEntry = nullptr;
        bool hasUnknownEntry = false;
        for (const WorkspaceEntry &entry : operationSnapshot.try_value()->entries)
        {
            if (entry.displayName == "Record.cuetrash" && entry.type == WorkspaceEntryType::RegularFile)
            {
                recordEntry = &entry;
            }
            else if (entry.displayName == "Payload" &&
                     (entry.type == WorkspaceEntryType::RegularFile || entry.type == WorkspaceEntryType::Directory))
            {
                payloadEntry = &entry;
            }
            else
            {
                hasUnknownEntry = true;
            }
        }
        if (recordEntry == nullptr || hasUnknownEntry)
        {
            addDiagnostic("Project trash operation contains an invalid entry set");
            continue;
        }
        Result<std::vector<std::byte>> bytes =
            m_workspace->read_file_bounded(*boundRecord.try_value(), k_maximumTrashRecordBytes);
        if (!bytes)
        {
            addDiagnostic("Project trash record could not be read", std::move(*bytes.try_error()));
            continue;
        }
        Result<project_files_private::TrashRecord> record = project_files_private::parse_trash_record(
            *bytes.try_value(), project_files_private::trash_record_hard_limits(), m_assertContext);
        if (!record || record.try_value()->projectId != m_projectId.text() ||
            record.try_value()->operationId != container.displayName)
        {
            if (!record)
            {
                addDiagnostic("Project trash record is invalid", std::move(*record.try_error()));
            }
            else
            {
                addDiagnostic("Project trash record identity does not match its container");
            }
            continue;
        }
        Result<RelativePath> original = RelativePath::parse(record.try_value()->originalPath, m_assertContext);
        if (!original)
        {
            addDiagnostic("Project trash original path is invalid", std::move(*original.try_error()));
            continue;
        }
        Result<BoundWorkspacePath> boundOriginal =
            m_workspace->bind_path(m_roots.source_assets(), std::move(*original.try_value()), m_assertContext);
        if (!boundOriginal)
        {
            addDiagnostic("Project trash original path could not be bound", std::move(*boundOriginal.try_error()));
            continue;
        }

        Result<WorkspaceEntryFingerprint> originalFingerprint =
            m_workspace->fingerprint_entry(*boundOriginal.try_value(), a_traversalLimits, a_contentLimits);
        Result<WorkspaceEntryFingerprint> payloadFingerprint =
            m_workspace->fingerprint_entry(*payload.try_value(), a_traversalLimits, a_contentLimits);
        const bool originalMissing =
            !originalFingerprint && is_io_error(*originalFingerprint.try_error(), IoError::NotFound);
        const bool payloadMissing =
            !payloadFingerprint && is_io_error(*payloadFingerprint.try_error(), IoError::NotFound);
        const bool originalMatches =
            originalFingerprint && *originalFingerprint.try_value() == record.try_value()->fingerprint;
        const bool payloadMatches =
            payloadFingerprint && *payloadFingerprint.try_value() == record.try_value()->fingerprint;

        /// @brief Reconciliationで確定した状態をRecordへAtomic反映する
        const auto updateRecord = [&](project_files_private::TrashRecordState a_state) noexcept -> bool
        {
            record.try_value()->state = a_state;
            Result<std::vector<std::byte>> updated =
                project_files_private::serialize_trash_record(*record.try_value(), m_assertContext);
            if (!updated)
            {
                addDiagnostic("Project trash reconciliation record serialization failed",
                              std::move(*updated.try_error()));
                return false;
            }
            WorkspaceMutationResult written =
                m_workspace->replace_file_atomic(*boundRecord.try_value(), *updated.try_value(), container.displayName);
            if (written.outcome == WorkspaceMutationOutcome::NotCommitted ||
                written.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
            {
                if (written.primaryError.has_value())
                {
                    addDiagnostic("Project trash reconciliation record write failed", std::move(*written.primaryError));
                }
                else
                {
                    addDiagnostic("Project trash reconciliation record write failed");
                }
                return false;
            }
            return true;
        };
        /// @brief Payloadを含まない完了済みRecordと空Operation ContainerだけをCleanupする
        const auto cleanupRecord = [&]() noexcept -> bool
        {
            WorkspaceMutationResult removedRecord =
                m_workspace->remove_file_or_empty_directory(*boundRecord.try_value());
            if (removedRecord.outcome == WorkspaceMutationOutcome::NotCommitted ||
                removedRecord.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
            {
                if (removedRecord.primaryError.has_value())
                {
                    addDiagnostic("Project trash completed record cleanup failed",
                                  std::move(*removedRecord.primaryError));
                }
                return false;
            }
            WorkspaceMutationResult removedOperation =
                m_workspace->remove_file_or_empty_directory(*operation.try_value());
            if (removedOperation.outcome == WorkspaceMutationOutcome::NotCommitted ||
                removedOperation.outcome == WorkspaceMutationOutcome::ReconciliationRequired)
            {
                if (removedOperation.primaryError.has_value())
                {
                    addDiagnostic("Project trash completed container cleanup failed",
                                  std::move(*removedOperation.primaryError));
                }
                return false;
            }
            return true;
        };
        /// @brief 検証済みRecordを指定状態のRecovery Catalog Entryへ変換する
        const auto addEntry = [&](RecoveryEntryState a_state) noexcept
        {
            RecoveryEntry entry;
            try
            {
                entry.operationId = container.displayName;
                entry.originalPath = record.try_value()->originalPath;
                entry.originalArea = ProjectFileArea::SourceAssets;
                entry.entryType = record.try_value()->fingerprint.type;
                entry.byteSize = fingerprint_byte_size(record.try_value()->fingerprint);
                entry.descendantCount = record.try_value()->fingerprint.manifest.size();
                entry.state = a_state;
                entries.push_back(std::move(entry));
            }
            catch (...)
            {
                terminate_allocation(m_assertContext);
            }
        };

        using project_files_private::TrashRecordState;
        switch (record.try_value()->state)
        {
        case TrashRecordState::Allocating:
            if (payloadEntry == nullptr && payloadMissing)
            {
                (void)cleanupRecord();
            }
            else
            {
                addEntry(RecoveryEntryState::ReconciliationRequired);
                addDiagnostic("Allocating trash operation contains an unexpected payload");
            }
            break;
        case TrashRecordState::Prepared:
            if (originalMissing && payloadMatches && updateRecord(TrashRecordState::Trashed))
            {
                addEntry(RecoveryEntryState::Recoverable);
            }
            else if (originalMatches && payloadMissing)
            {
                (void)cleanupRecord();
            }
            else
            {
                addEntry(RecoveryEntryState::ReconciliationRequired);
                addDiagnostic("Prepared trash operation could not be reconciled automatically");
            }
            break;
        case TrashRecordState::Trashed:
            if (originalMissing && payloadMatches && payloadEntry != nullptr)
            {
                addEntry(RecoveryEntryState::Recoverable);
            }
            else if (originalMatches && payloadMissing)
            {
                (void)cleanupRecord();
            }
            else
            {
                addEntry(RecoveryEntryState::ReconciliationRequired);
                addDiagnostic("Trashed operation does not match its expected data location");
            }
            break;
        case TrashRecordState::Restoring:
            if (originalMatches && payloadMissing && updateRecord(TrashRecordState::Restored))
            {
                (void)cleanupRecord();
            }
            else if (originalMissing && payloadMatches && updateRecord(TrashRecordState::Trashed))
            {
                addEntry(RecoveryEntryState::Recoverable);
            }
            else
            {
                addEntry(RecoveryEntryState::ReconciliationRequired);
                addDiagnostic("Restoring operation could not be reconciled automatically");
            }
            break;
        case TrashRecordState::Restored:
            if (originalMatches && payloadMissing)
            {
                (void)cleanupRecord();
            }
            else if (originalMissing && payloadMatches && updateRecord(TrashRecordState::Trashed))
            {
                addEntry(RecoveryEntryState::Recoverable);
            }
            else
            {
                addEntry(RecoveryEntryState::ReconciliationRequired);
                addDiagnostic("Restored operation does not match its expected data location");
            }
            break;
        }
    }
    try
    {
        std::sort(entries.begin(), entries.end(),
                  /// @brief Recovery EntryをOperation IDの決定順へ並べる
                  [](const RecoveryEntry &a_left, const RecoveryEntry &a_right) noexcept
                  { return a_left.operationId < a_right.operationId; });
        m_recoveryEntries = std::move(entries);
        m_recoveryDiagnostics = std::move(diagnostics);
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    return Result<void>::success();
}

Result<ProjectFileOperationResult> ProjectFileService::execute_create(ProjectFileOperationKind a_kind,
                                                                      ProjectFileArea a_area,
                                                                      RelativePath a_destination,
                                                                      std::span<const std::byte> a_bytes) noexcept
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Result<ProjectFileOperationResult>::failure(make_project_file_error(
            m_assertContext, ProjectFileError::InvalidRequest, "Project file service was called from another thread"));
    }
    if (m_isBusy)
    {
        return Result<ProjectFileOperationResult>::failure(make_project_file_error(
            m_assertContext, ProjectFileError::Busy, "Project file mutation is already active"));
    }
    BusyReset busy(m_isBusy);

    Result<std::string> generatedId = m_operationIdSource->next_operation_id();
    if (!generatedId)
    {
        return Result<ProjectFileOperationResult>::failure(reclassify_project_file_error(
            m_assertContext, ProjectFileError::InvalidRequest, "Project file operation id generation failed",
            std::move(*generatedId.try_error())));
    }
    Result<ProjectId> validatedId = ProjectId::parse(*generatedId.try_value(), m_assertContext);
    if (!validatedId)
    {
        return Result<ProjectFileOperationResult>::failure(
            reclassify_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                          "Project file operation id is invalid", std::move(*validatedId.try_error())));
    }

    std::string operationId = std::move(*generatedId.try_value());
    std::string destination;
    try
    {
        destination.assign(a_destination.text());
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }

    if (!m_policy.can_mutate(a_area))
    {
        std::optional<Error> primary(make_project_file_error(m_assertContext, ProjectFileError::ProtectedEntry,
                                                             "Project file area is protected from mutation"));
        ProjectFileOperationResult result(std::move(operationId), a_kind, a_area, std::nullopt, std::move(destination),
                                          ProjectFileOperationStage::ValidateRequest,
                                          ProjectFileOperationOutcome::NotCommitted, std::move(primary), {}, {});
        return Result<ProjectFileOperationResult>::success(std::move(result));
    }

    Result<BoundWorkspacePath> bound = m_workspace->bind_path(area_root(a_area), a_destination, m_assertContext);
    if (!bound)
    {
        const ProjectFileError classification =
            classify_project_file_error(*bound.try_error(), WorkspaceMutationOutcome::NotCommitted);
        std::optional<Error> primary(reclassify_project_file_error(
            m_assertContext, classification, "Project file destination binding failed", std::move(*bound.try_error())));
        ProjectFileOperationResult result(std::move(operationId), a_kind, a_area, std::nullopt, std::move(destination),
                                          ProjectFileOperationStage::BindDestination,
                                          ProjectFileOperationOutcome::NotCommitted, std::move(primary), {}, {});
        return Result<ProjectFileOperationResult>::success(std::move(result));
    }

    WorkspaceMutationResult mutation =
        a_kind == ProjectFileOperationKind::DirectoryCreation
            ? m_workspace->create_directory_new(*bound.try_value(), operationId)
            : m_workspace->create_file_new_atomic(*bound.try_value(), a_bytes, operationId);

    std::optional<Error> primary;
    if (mutation.primaryError.has_value())
    {
        const ProjectFileError classification = classify_project_file_error(*mutation.primaryError, mutation.outcome);
        primary.emplace(reclassify_project_file_error(m_assertContext, classification,
                                                      "Project file create operation failed",
                                                      std::move(*mutation.primaryError)));
    }
    else if (mutation.outcome != WorkspaceMutationOutcome::Committed)
    {
        primary.emplace(make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                "Project file create result requires reconciliation"));
    }

    std::vector<std::string> rescanDirectories;
    try
    {
        const std::size_t separator = destination.rfind('/');
        rescanDirectories.emplace_back(separator == std::string::npos ? std::string{}
                                                                      : destination.substr(0U, separator));
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }

    const ProjectFileOperationStage stage =
        mutation.outcome == WorkspaceMutationOutcome::Committed
            ? ProjectFileOperationStage::Complete
            : (mutation.outcome == WorkspaceMutationOutcome::NotCommitted ? ProjectFileOperationStage::NativePublish
                                                                          : ProjectFileOperationStage::Verify);
    ProjectFileOperationResult result(std::move(operationId), a_kind, a_area, std::nullopt, std::move(destination),
                                      stage, convert_outcome(mutation.outcome), std::move(primary),
                                      std::move(mutation.secondaryDiagnostics), std::move(rescanDirectories));
    return Result<ProjectFileOperationResult>::success(std::move(result));
}

Result<ProjectFileOperationResult> ProjectFileService::execute_transfer(
    ProjectFileOperationKind a_kind, ProjectFileArea a_area, RelativePath a_source, RelativePath a_destination,
    TraversalLimits a_traversalLimits, ContentVerificationLimits a_contentLimits) noexcept
{
    if (std::this_thread::get_id() != m_ownerThread)
    {
        return Result<ProjectFileOperationResult>::failure(make_project_file_error(
            m_assertContext, ProjectFileError::InvalidRequest, "Project file service was called from another thread"));
    }
    if (m_isBusy)
    {
        return Result<ProjectFileOperationResult>::failure(make_project_file_error(
            m_assertContext, ProjectFileError::Busy, "Project file mutation is already active"));
    }
    BusyReset busy(m_isBusy);

    Result<std::string> generatedId = m_operationIdSource->next_operation_id();
    if (!generatedId)
    {
        return Result<ProjectFileOperationResult>::failure(reclassify_project_file_error(
            m_assertContext, ProjectFileError::InvalidRequest, "Project file operation id generation failed",
            std::move(*generatedId.try_error())));
    }
    Result<ProjectId> validatedId = ProjectId::parse(*generatedId.try_value(), m_assertContext);
    if (!validatedId)
    {
        return Result<ProjectFileOperationResult>::failure(
            reclassify_project_file_error(m_assertContext, ProjectFileError::InvalidRequest,
                                          "Project file operation id is invalid", std::move(*validatedId.try_error())));
    }

    std::string operationId = std::move(*generatedId.try_value());
    std::string source;
    std::string destination;
    try
    {
        source.assign(a_source.text());
        destination.assign(a_destination.text());
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }

    const std::string sourceKey = a_source.comparison_key(m_assertContext);
    const std::string destinationKey = a_destination.comparison_key(m_assertContext);
    const std::size_t sourceSeparator = source.rfind('/');
    const std::size_t destinationSeparator = destination.rfind('/');
    const std::string_view sourceParent = sourceSeparator == std::string::npos
                                              ? std::string_view{}
                                              : std::string_view(source).substr(0U, sourceSeparator);
    const std::string_view destinationParent = destinationSeparator == std::string::npos
                                                   ? std::string_view{}
                                                   : std::string_view(destination).substr(0U, destinationSeparator);
    const std::size_t sourceKeySeparator = sourceKey.rfind('/');
    const std::size_t destinationKeySeparator = destinationKey.rfind('/');
    const std::string_view sourceParentKey = sourceKeySeparator == std::string::npos
                                                 ? std::string_view{}
                                                 : std::string_view(sourceKey).substr(0U, sourceKeySeparator);
    const std::string_view destinationParentKey =
        destinationKeySeparator == std::string::npos
            ? std::string_view{}
            : std::string_view(destinationKey).substr(0U, destinationKeySeparator);
    const bool samePath = source == destination;
    const bool samePortablePath = sourceKey == destinationKey;
    const bool destinationIsDescendant = destinationKey.size() > sourceKey.size() &&
                                         destinationKey.starts_with(sourceKey) &&
                                         destinationKey[sourceKey.size()] == '/';
    const bool invalidKind = a_kind != ProjectFileOperationKind::Rename && a_kind != ProjectFileOperationKind::Move &&
                             a_kind != ProjectFileOperationKind::Copy;
    const bool wrongRenameParent = a_kind == ProjectFileOperationKind::Rename && sourceParent != destinationParent;
    const bool wrongMoveParent = a_kind == ProjectFileOperationKind::Move && sourceParentKey == destinationParentKey;
    const bool invalidCopyLimits = a_kind == ProjectFileOperationKind::Copy && !a_contentLimits.is_valid();
    if (!m_policy.can_mutate(a_area) || invalidKind || !a_traversalLimits.is_valid() || invalidCopyLimits || samePath ||
        (samePortablePath && a_kind != ProjectFileOperationKind::Rename) || wrongRenameParent || wrongMoveParent ||
        destinationIsDescendant)
    {
        const ProjectFileError errorCode =
            !m_policy.can_mutate(a_area) ? ProjectFileError::ProtectedEntry : ProjectFileError::InvalidRequest;
        std::optional<Error> primary(make_project_file_error(
            m_assertContext, errorCode,
            !m_policy.can_mutate(a_area) ? "Project file area is protected from mutation"
                                         : "Project file transfer request violates semantic preconditions"));
        ProjectFileOperationResult result(std::move(operationId), a_kind, a_area, std::move(source),
                                          std::move(destination), ProjectFileOperationStage::ValidateRequest,
                                          ProjectFileOperationOutcome::NotCommitted, std::move(primary), {}, {});
        return Result<ProjectFileOperationResult>::success(std::move(result));
    }

    Result<BoundWorkspacePath> boundSource = m_workspace->bind_path(area_root(a_area), a_source, m_assertContext);
    if (!boundSource)
    {
        const ProjectFileError classification =
            classify_project_file_error(*boundSource.try_error(), WorkspaceMutationOutcome::NotCommitted);
        std::optional<Error> primary(reclassify_project_file_error(m_assertContext, classification,
                                                                   "Project file source binding failed",
                                                                   std::move(*boundSource.try_error())));
        ProjectFileOperationResult result(std::move(operationId), a_kind, a_area, std::move(source),
                                          std::move(destination), ProjectFileOperationStage::BindSource,
                                          ProjectFileOperationOutcome::NotCommitted, std::move(primary), {}, {});
        return Result<ProjectFileOperationResult>::success(std::move(result));
    }
    Result<BoundWorkspacePath> boundDestination =
        m_workspace->bind_path(area_root(a_area), a_destination, m_assertContext);
    if (!boundDestination)
    {
        const ProjectFileError classification =
            classify_project_file_error(*boundDestination.try_error(), WorkspaceMutationOutcome::NotCommitted);
        std::optional<Error> primary(reclassify_project_file_error(m_assertContext, classification,
                                                                   "Project file destination binding failed",
                                                                   std::move(*boundDestination.try_error())));
        ProjectFileOperationResult result(std::move(operationId), a_kind, a_area, std::move(source),
                                          std::move(destination), ProjectFileOperationStage::BindDestination,
                                          ProjectFileOperationOutcome::NotCommitted, std::move(primary), {}, {});
        return Result<ProjectFileOperationResult>::success(std::move(result));
    }

    WorkspaceMutationResult mutation =
        a_kind == ProjectFileOperationKind::Copy
            ? m_workspace->copy_entry_new(*boundSource.try_value(), *boundDestination.try_value(), a_traversalLimits,
                                          a_contentLimits, operationId)
            : m_workspace->rename_entry(*boundSource.try_value(), *boundDestination.try_value(), a_traversalLimits);
    std::optional<Error> primary;
    if (mutation.primaryError.has_value())
    {
        const ProjectFileError classification = classify_project_file_error(*mutation.primaryError, mutation.outcome);
        primary.emplace(reclassify_project_file_error(m_assertContext, classification,
                                                      "Project file transfer operation failed",
                                                      std::move(*mutation.primaryError)));
    }
    else if (mutation.outcome != WorkspaceMutationOutcome::Committed)
    {
        primary.emplace(make_project_file_error(m_assertContext, ProjectFileError::RecoveryRequired,
                                                "Project file transfer result requires reconciliation"));
    }

    std::vector<std::string> rescanDirectories;
    try
    {
        rescanDirectories.emplace_back(sourceParent);
        if (sourceParentKey != destinationParentKey)
        {
            rescanDirectories.emplace_back(destinationParent);
        }
    }
    catch (...)
    {
        terminate_allocation(m_assertContext);
    }
    const ProjectFileOperationStage stage =
        mutation.outcome == WorkspaceMutationOutcome::Committed
            ? ProjectFileOperationStage::Complete
            : (mutation.outcome == WorkspaceMutationOutcome::NotCommitted ? ProjectFileOperationStage::NativePublish
                                                                          : ProjectFileOperationStage::Verify);
    ProjectFileOperationResult result(std::move(operationId), a_kind, a_area, std::move(source), std::move(destination),
                                      stage, convert_outcome(mutation.outcome), std::move(primary),
                                      std::move(mutation.secondaryDiagnostics), std::move(rescanDirectories));
    return Result<ProjectFileOperationResult>::success(std::move(result));
}

const RelativePath &ProjectFileService::area_root(ProjectFileArea a_area) const noexcept
{
    switch (a_area)
    {
    case ProjectFileArea::SourceAssets:
        return m_roots.source_assets();
    case ProjectFileArea::RuntimeAssets:
        return m_roots.runtime_assets();
    case ProjectFileArea::Generated:
        return m_roots.generated();
    case ProjectFileArea::Saved:
        return m_roots.saved();
    }
    return m_roots.source_assets();
}
} // namespace cue::project_files
