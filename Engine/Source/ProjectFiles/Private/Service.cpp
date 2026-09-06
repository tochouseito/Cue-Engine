#include <Cue/ProjectFiles/Service.h>

#include <Cue/IO/Error.h>
#include <Cue/ProjectFiles/Error.h>

#include <cstdlib>
#include <utility>

namespace
{
constexpr std::size_t k_maximumProjectDescriptorBytes = 1024U * 1024U;

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
        return Result<ProjectFileService>::failure(reclassify_project_file_error(
            a_assertContext,
            classify_project_file_error(*descriptorBytes.try_error(), WorkspaceMutationOutcome::NotCommitted),
            "Project descriptor could not be read from workspace", std::move(*descriptorBytes.try_error())));
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
        return Result<ProjectFileService>::failure(reclassify_project_file_error(
            a_assertContext,
            classify_project_file_error(*sourceRoot.try_error(), WorkspaceMutationOutcome::NotCommitted),
            "Project source assets root could not be bound", std::move(*sourceRoot.try_error())));
    }
    Result<void> verifiedSourceRoot = a_workspace->verify_directory(*sourceRoot.try_value());
    if (!verifiedSourceRoot)
    {
        return Result<ProjectFileService>::failure(reclassify_project_file_error(
            a_assertContext,
            classify_project_file_error(*verifiedSourceRoot.try_error(), WorkspaceMutationOutcome::NotCommitted),
            "Project source assets root is unavailable", std::move(*verifiedSourceRoot.try_error())));
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
