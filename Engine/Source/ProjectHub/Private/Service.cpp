#include <Cue/ProjectHub/Service.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/IO/Filesystem.h>
#include <Cue/ProjectHub/Error.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace
{
constexpr std::size_t k_maximumLocatorBytes = 4096U;

[[nodiscard]] bool is_valid_locator_text(std::string_view a_text) noexcept
{
    if (a_text.empty() || a_text.size() > k_maximumLocatorBytes)
    {
        return false;
    }
    return std::none_of(a_text.begin(), a_text.end(),
                        [](char a_character) { return static_cast<unsigned char>(a_character) < 0x20U; });
}

[[nodiscard]] std::string make_version_text(const cue::EngineVersion &a_version)
{
    return std::to_string(a_version.major) + "." + std::to_string(a_version.minor) + "." +
           std::to_string(a_version.patch);
}

[[nodiscard]] std::string make_compatibility_id(const cue::EngineCompatibility &a_compatibility)
{
    std::string identifier = "cue-engine:[" + make_version_text(a_compatibility.minimum) + ",";
    if (a_compatibility.maximumExclusive.has_value())
    {
        identifier.append(make_version_text(*a_compatibility.maximumExclusive));
    }
    identifier.append(")");
    return identifier;
}

[[nodiscard]] cue::project_hub::ProjectEntryState map_locator_state(cue::ProjectLocatorState a_state) noexcept
{
    switch (a_state)
    {
    case cue::ProjectLocatorState::Available:
        return cue::project_hub::ProjectEntryState::Available;
    case cue::ProjectLocatorState::Missing:
        return cue::project_hub::ProjectEntryState::Missing;
    case cue::ProjectLocatorState::Moved:
        return cue::project_hub::ProjectEntryState::Moved;
    }
    return cue::project_hub::ProjectEntryState::Broken;
}
} // namespace

namespace cue::project_hub
{
EditorLaunchRequest::EditorLaunchRequest(std::string &&a_projectDescriptorLocator, std::string &&a_expectedProjectId,
                                         std::string &&a_engineCompatibilityId,
                                         std::optional<std::string> &&a_initialSceneLocator) noexcept
    : m_projectDescriptorLocator(std::move(a_projectDescriptorLocator)),
      m_expectedProjectId(std::move(a_expectedProjectId)), m_engineCompatibilityId(std::move(a_engineCompatibilityId)),
      m_initialSceneLocator(std::move(a_initialSceneLocator))
{
}

std::uint32_t EditorLaunchRequest::protocol_version() const noexcept
{
    return k_editorLaunchProtocolVersion;
}

std::string_view EditorLaunchRequest::project_descriptor_locator() const noexcept
{
    return m_projectDescriptorLocator;
}

std::string_view EditorLaunchRequest::expected_project_id() const noexcept
{
    return m_expectedProjectId;
}

std::string_view EditorLaunchRequest::engine_compatibility_id() const noexcept
{
    return m_engineCompatibilityId;
}

const std::optional<std::string> &EditorLaunchRequest::initial_scene_locator() const noexcept
{
    return m_initialSceneLocator;
}

ProjectHubService::ProjectHubService(ConstructionKey, FilesystemRoot &a_workspaceFilesystem,
                                     ProjectHubPlatform &a_platform, ProjectHubConfiguration &&a_configuration,
                                     RecentProjectRegistry &&a_registry, const AssertContext &a_assertContext) noexcept
    : m_workspaceFilesystem(&a_workspaceFilesystem), m_platform(&a_platform), m_assertContext(&a_assertContext),
      m_configuration(std::move(a_configuration)), m_registry(std::move(a_registry))
{
}

Result<std::unique_ptr<ProjectHubService>> ProjectHubService::create(FilesystemRoot &a_workspaceFilesystem,
                                                                     ProjectHubPlatform &a_platform,
                                                                     ProjectHubConfiguration &&a_configuration,
                                                                     const AssertContext &a_assertContext) noexcept
{
    try
    {
        if (a_configuration.supportedProjectFormatVersion == 0U ||
            (a_configuration.blankProjectCompatibility.maximumExclusive.has_value() &&
             *a_configuration.blankProjectCompatibility.maximumExclusive <=
                 a_configuration.blankProjectCompatibility.minimum))
        {
            return Result<std::unique_ptr<ProjectHubService>>::failure(make_project_hub_error(
                a_assertContext, ProjectHubError::InvalidConfiguration, "Project Hub configuration is invalid"));
        }
        auto registry = load_recent_project_registry(a_workspaceFilesystem, a_assertContext);
        if (!registry)
        {
            return Result<std::unique_ptr<ProjectHubService>>::failure(reclassify_project_hub_error(
                a_assertContext, ProjectHubError::PersistenceFailure, "Project Hub registry could not be loaded",
                std::move(*registry.try_error())));
        }
        std::unique_ptr<ProjectHubService> service(
            new ProjectHubService(ConstructionKey{}, a_workspaceFilesystem, a_platform, std::move(a_configuration),
                                  std::move(*registry.try_value()), a_assertContext));
        service->m_templates.push_back(ProjectTemplateView{std::string(k_blank3dTemplateId), "Blank 3D",
                                                           service->m_configuration.blankProjectCompatibility});
        auto refreshed = service->refresh();
        if (!refreshed)
        {
            return Result<std::unique_ptr<ProjectHubService>>::failure(std::move(*refreshed.try_error()));
        }
        return Result<std::unique_ptr<ProjectHubService>>::success(std::move(service));
    }
    catch (...)
    {
        a_assertContext.fatal_handler().terminate("Cue.ProjectHub service construction failed");
    }
    std::terminate();
}

std::span<const ProjectTemplateView> ProjectHubService::templates() const noexcept
{
    return m_templates;
}

std::span<const ProjectRowView> ProjectHubService::projects() const noexcept
{
    return m_projects;
}

Result<void> ProjectHubService::refresh() noexcept
{
    try
    {
        std::vector<ProjectRowView> projects;
        projects.reserve(m_registry.entries().size());
        bool registryChanged = false;
        for (const RecentProject &entry : m_registry.entries())
        {
            ProjectRowView row{std::string(entry.project_id().text()),
                               std::string(entry.locator()),
                               std::string(entry.locator()),
                               entry.last_opened_milliseconds(),
                               entry.is_pinned(),
                               map_locator_state(entry.locator_state()),
                               ProjectEntryProblem::None,
                               ProjectCompatibilityStatus::Unknown,
                               false,
                               std::nullopt,
                               {}};

            auto root = m_platform->open_root(entry.locator());
            if (!root)
            {
                row.state = ProjectEntryState::Broken;
                row.problem = ProjectEntryProblem::LocatorAccessFailed;
                projects.push_back(std::move(row));
                continue;
            }
            if (*root.try_value() == nullptr)
            {
                row.state = ProjectEntryState::Missing;
                if (entry.locator_state() != ProjectLocatorState::Missing)
                {
                    auto marked = m_registry.mark_project_missing(entry.project_id(), *m_assertContext);
                    if (!marked)
                    {
                        return marked;
                    }
                    registryChanged = true;
                }
                projects.push_back(std::move(row));
                continue;
            }

            auto descriptor = load_project_descriptor(**root.try_value(), *m_assertContext);
            if (!descriptor)
            {
                row.state = ProjectEntryState::Broken;
                row.problem = ProjectEntryProblem::DescriptorInvalid;
                projects.push_back(std::move(row));
                continue;
            }
            if (descriptor.try_value()->project_id() != entry.project_id())
            {
                row.state = ProjectEntryState::Broken;
                row.problem = ProjectEntryProblem::IdentityMismatch;
                projects.push_back(std::move(row));
                continue;
            }

            row.displayName = std::string(descriptor.try_value()->display_name());
            row.engineCompatibility = descriptor.try_value()->engine_compatibility();
            if (entry.locator_state() == ProjectLocatorState::Missing)
            {
                auto marked = m_registry.mark_project_available(entry.project_id(), *m_assertContext);
                if (!marked)
                {
                    return marked;
                }
                row.state = ProjectEntryState::Available;
                registryChanged = true;
            }
            auto compatibility = evaluate_project_compatibility(
                descriptor.try_value()->schema_version(), m_configuration.supportedProjectFormatVersion,
                descriptor.try_value()->engine_compatibility(), m_configuration.currentEngineVersion,
                m_configuration.capabilityProfile, m_configuration.capabilitySnapshot, *m_assertContext);
            if (!compatibility)
            {
                row.state = ProjectEntryState::Broken;
                row.problem = ProjectEntryProblem::CompatibilityInvalid;
                projects.push_back(std::move(row));
                continue;
            }
            row.compatibilityStatus = compatibility.try_value()->status();
            row.canOpen = compatibility.try_value()->can_open();
            row.compatibilityReasons.assign(compatibility.try_value()->reasons().begin(),
                                            compatibility.try_value()->reasons().end());
            projects.push_back(std::move(row));
        }
        if (registryChanged)
        {
            auto saved = save_recent_project_registry(*m_workspaceFilesystem, m_registry, *m_assertContext);
            if (!saved)
            {
                return Result<void>::failure(reclassify_project_hub_error(
                    *m_assertContext, ProjectHubError::PersistenceFailure,
                    "Project Hub registry state could not be saved", std::move(*saved.try_error())));
            }
        }
        m_projects = std::move(projects);
        return Result<void>::success();
    }
    catch (...)
    {
        m_assertContext->fatal_handler().terminate("Cue.ProjectHub refresh failed");
    }
    std::terminate();
}

Result<void> ProjectHubService::create_blank_project(std::string_view a_parentLocator, std::string_view a_projectName,
                                                     std::string_view a_displayName, std::string_view a_templateId,
                                                     std::uint64_t a_openedMilliseconds) noexcept
{
    if (a_templateId != k_blank3dTemplateId)
    {
        return Result<void>::failure(make_project_hub_error(*m_assertContext, ProjectHubError::InvalidTemplate,
                                                            "Project template is not registered"));
    }
    auto parentLocator = m_platform->normalize_project_locator(a_parentLocator);
    if (!parentLocator)
    {
        return Result<void>::failure(reclassify_project_hub_error(*m_assertContext, ProjectHubError::InvalidLocator,
                                                                  "Project parent locator is invalid",
                                                                  std::move(*parentLocator.try_error())));
    }
    auto parentRoot = m_platform->open_root(*parentLocator.try_value());
    if (!parentRoot)
    {
        return Result<void>::failure(reclassify_project_hub_error(*m_assertContext, ProjectHubError::InvalidLocator,
                                                                  "Project parent locator could not be opened",
                                                                  std::move(*parentRoot.try_error())));
    }
    if (*parentRoot.try_value() == nullptr)
    {
        return Result<void>::failure(make_project_hub_error(*m_assertContext, ProjectHubError::ProjectMissing,
                                                            "Project parent locator does not exist"));
    }
    auto projectLocator = m_platform->compose_project_locator(*parentLocator.try_value(), a_projectName);
    if (!projectLocator)
    {
        return Result<void>::failure(reclassify_project_hub_error(*m_assertContext, ProjectHubError::InvalidLocator,
                                                                  "Project locator could not be composed",
                                                                  std::move(*projectLocator.try_error())));
    }
    auto projectId = m_platform->next_project_id();
    if (!projectId)
    {
        return Result<void>::failure(std::move(*projectId.try_error()));
    }
    auto descriptor =
        generate_blank_project(**parentRoot.try_value(), a_projectName, a_displayName, *projectId.try_value(),
                               BlankProjectTemplate{m_configuration.blankProjectCompatibility}, *m_assertContext);
    if (!descriptor)
    {
        return Result<void>::failure(std::move(*descriptor.try_error()));
    }
    auto registered = m_registry.register_project(*descriptor.try_value(), *projectLocator.try_value(),
                                                  a_openedMilliseconds, *m_assertContext);
    if (!registered)
    {
        return registered;
    }
    return save_and_refresh();
}

Result<void> ProjectHubService::register_project(std::string_view a_locator, std::uint64_t a_openedMilliseconds,
                                                 bool a_confirmMovedProject) noexcept
{
    auto locator = m_platform->normalize_project_locator(a_locator);
    if (!locator)
    {
        return Result<void>::failure(reclassify_project_hub_error(*m_assertContext, ProjectHubError::InvalidLocator,
                                                                  "Project locator is invalid",
                                                                  std::move(*locator.try_error())));
    }
    auto root = m_platform->open_root(*locator.try_value());
    if (!root)
    {
        return Result<void>::failure(reclassify_project_hub_error(*m_assertContext, ProjectHubError::ProjectBroken,
                                                                  "Project locator could not be opened",
                                                                  std::move(*root.try_error())));
    }
    if (*root.try_value() == nullptr)
    {
        return Result<void>::failure(make_project_hub_error(*m_assertContext, ProjectHubError::ProjectMissing,
                                                            "Project locator does not exist"));
    }
    auto descriptor = load_project_descriptor(**root.try_value(), *m_assertContext);
    if (!descriptor)
    {
        return Result<void>::failure(reclassify_project_hub_error(*m_assertContext, ProjectHubError::ProjectBroken,
                                                                  "Project descriptor could not be loaded",
                                                                  std::move(*descriptor.try_error())));
    }
    Result<void> registered = a_confirmMovedProject
                                  ? m_registry.reassociate_project(*descriptor.try_value(), *locator.try_value(),
                                                                   a_openedMilliseconds, *m_assertContext)
                                  : m_registry.register_project(*descriptor.try_value(), *locator.try_value(),
                                                                a_openedMilliseconds, *m_assertContext);
    if (!registered)
    {
        return registered;
    }
    return save_and_refresh();
}

Result<EditorLaunchRequest> ProjectHubService::open_project(
    std::string_view a_projectId, std::uint64_t a_openedMilliseconds,
    std::optional<std::string_view> a_initialSceneLocator) noexcept
{
    try
    {
        auto projectId = parse_project_id(a_projectId);
        if (!projectId)
        {
            return Result<EditorLaunchRequest>::failure(std::move(*projectId.try_error()));
        }
        const auto found = std::find_if(m_registry.entries().begin(), m_registry.entries().end(),
                                        [&projectId](const RecentProject &a_entry)
                                        { return a_entry.project_id() == *projectId.try_value(); });
        if (found == m_registry.entries().end())
        {
            return Result<EditorLaunchRequest>::failure(make_project_hub_error(
                *m_assertContext, ProjectHubError::ProjectNotFound, "Project is not in the Recent registry"));
        }
        auto root = m_platform->open_root(found->locator());
        if (!root)
        {
            return Result<EditorLaunchRequest>::failure(
                reclassify_project_hub_error(*m_assertContext, ProjectHubError::ProjectBroken,
                                             "Project locator could not be opened", std::move(*root.try_error())));
        }
        if (*root.try_value() == nullptr)
        {
            auto marked = m_registry.mark_project_missing(*projectId.try_value(), *m_assertContext);
            if (!marked)
            {
                return Result<EditorLaunchRequest>::failure(std::move(*marked.try_error()));
            }
            auto saved = save_recent_project_registry(*m_workspaceFilesystem, m_registry, *m_assertContext);
            if (!saved)
            {
                return Result<EditorLaunchRequest>::failure(reclassify_project_hub_error(
                    *m_assertContext, ProjectHubError::PersistenceFailure, "Missing project state could not be saved",
                    std::move(*saved.try_error())));
            }
            return Result<EditorLaunchRequest>::failure(make_project_hub_error(
                *m_assertContext, ProjectHubError::ProjectMissing, "Project locator does not exist"));
        }
        auto descriptor = load_project_descriptor(**root.try_value(), *m_assertContext);
        if (!descriptor)
        {
            return Result<EditorLaunchRequest>::failure(reclassify_project_hub_error(
                *m_assertContext, ProjectHubError::ProjectBroken, "Project descriptor could not be loaded",
                std::move(*descriptor.try_error())));
        }
        if (descriptor.try_value()->project_id() != *projectId.try_value())
        {
            return Result<EditorLaunchRequest>::failure(
                make_project_hub_error(*m_assertContext, ProjectHubError::ProjectIdentityMismatch,
                                       "Project descriptor identity differs from the Recent registry"));
        }
        auto compatibility = evaluate_project_compatibility(
            descriptor.try_value()->schema_version(), m_configuration.supportedProjectFormatVersion,
            descriptor.try_value()->engine_compatibility(), m_configuration.currentEngineVersion,
            m_configuration.capabilityProfile, m_configuration.capabilitySnapshot, *m_assertContext);
        if (!compatibility)
        {
            return Result<EditorLaunchRequest>::failure(std::move(*compatibility.try_error()));
        }
        if (!compatibility.try_value()->can_open())
        {
            return Result<EditorLaunchRequest>::failure(make_project_hub_error(
                *m_assertContext, ProjectHubError::ProjectUnsupported, "Project cannot be opened by this engine"));
        }
        std::optional<std::string> sceneLocator;
        if (a_initialSceneLocator.has_value())
        {
            if (!is_valid_locator_text(*a_initialSceneLocator))
            {
                return Result<EditorLaunchRequest>::failure(make_project_hub_error(
                    *m_assertContext, ProjectHubError::InvalidSceneLocator, "Initial scene locator is invalid"));
            }
            sceneLocator = std::string(*a_initialSceneLocator);
        }
        auto descriptorLocator = m_platform->compose_descriptor_locator(found->locator());
        if (!descriptorLocator)
        {
            return Result<EditorLaunchRequest>::failure(reclassify_project_hub_error(
                *m_assertContext, ProjectHubError::InvalidLocator, "Project descriptor locator could not be composed",
                std::move(*descriptorLocator.try_error())));
        }
        std::string expectedProjectId(descriptor.try_value()->project_id().text());
        std::string compatibilityId = make_compatibility_id(descriptor.try_value()->engine_compatibility());
        std::string projectLocator(found->locator());
        auto registered = m_registry.register_project(*descriptor.try_value(), projectLocator, a_openedMilliseconds,
                                                      *m_assertContext);
        if (!registered)
        {
            return Result<EditorLaunchRequest>::failure(std::move(*registered.try_error()));
        }
        auto saved = save_recent_project_registry(*m_workspaceFilesystem, m_registry, *m_assertContext);
        if (!saved)
        {
            return Result<EditorLaunchRequest>::failure(
                reclassify_project_hub_error(*m_assertContext, ProjectHubError::PersistenceFailure,
                                             "Project open state could not be saved", std::move(*saved.try_error())));
        }
        auto refreshed = refresh();
        if (!refreshed)
        {
            return Result<EditorLaunchRequest>::failure(std::move(*refreshed.try_error()));
        }
        return Result<EditorLaunchRequest>::success(
            EditorLaunchRequest(std::move(*descriptorLocator.try_value()), std::move(expectedProjectId),
                                std::move(compatibilityId), std::move(sceneLocator)));
    }
    catch (...)
    {
        m_assertContext->fatal_handler().terminate("Cue.ProjectHub open request generation failed");
    }
    std::terminate();
}

Result<void> ProjectHubService::set_project_pinned(std::string_view a_projectId, bool a_isPinned) noexcept
{
    auto projectId = parse_project_id(a_projectId);
    if (!projectId)
    {
        return Result<void>::failure(std::move(*projectId.try_error()));
    }
    auto changed = m_registry.set_project_pinned(*projectId.try_value(), a_isPinned, *m_assertContext);
    return changed ? save_and_refresh() : std::move(changed);
}

Result<void> ProjectHubService::move_pinned_project(std::string_view a_projectId, std::size_t a_targetIndex) noexcept
{
    auto projectId = parse_project_id(a_projectId);
    if (!projectId)
    {
        return Result<void>::failure(std::move(*projectId.try_error()));
    }
    auto changed = m_registry.move_pinned_project(*projectId.try_value(), a_targetIndex, *m_assertContext);
    return changed ? save_and_refresh() : std::move(changed);
}

Result<void> ProjectHubService::remove_project(std::string_view a_projectId) noexcept
{
    auto projectId = parse_project_id(a_projectId);
    if (!projectId)
    {
        return Result<void>::failure(std::move(*projectId.try_error()));
    }
    auto removed = m_registry.remove_project(*projectId.try_value(), *m_assertContext);
    return removed ? save_and_refresh() : std::move(removed);
}

Result<void> ProjectHubService::save_and_refresh() noexcept
{
    auto saved = save_recent_project_registry(*m_workspaceFilesystem, m_registry, *m_assertContext);
    if (!saved)
    {
        return Result<void>::failure(reclassify_project_hub_error(*m_assertContext, ProjectHubError::PersistenceFailure,
                                                                  "Project Hub registry could not be saved",
                                                                  std::move(*saved.try_error())));
    }
    return refresh();
}

Result<ProjectId> ProjectHubService::parse_project_id(std::string_view a_projectId) const noexcept
{
    return ProjectId::parse(a_projectId, *m_assertContext);
}
} // namespace cue::project_hub
