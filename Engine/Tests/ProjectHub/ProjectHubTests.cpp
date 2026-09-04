#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/IO/Windows/WindowsFilesystem.h>
#include <Cue/ProjectHub/Error.h>
#include <Cue/ProjectHub/Service.h>

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    [[noreturn]] void terminate() noexcept override
    {
        std::terminate();
    }

    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::terminate();
    }
};

[[nodiscard]] std::string to_utf8(const std::filesystem::path &a_path)
{
    const std::u8string text = a_path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

class TestDirectory final
{
  public:
    TestDirectory()
    {
        m_path = std::filesystem::temp_directory_path() /
                 ("CueProjectHubTests-" + std::to_string(static_cast<unsigned long>(GetCurrentProcessId())));
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        std::filesystem::create_directories(m_path / "Workspace", error);
        std::filesystem::create_directories(m_path / "Projects", error);
        m_valid = !error;
    }

    TestDirectory(const TestDirectory &) = delete;
    TestDirectory &operator=(const TestDirectory &) = delete;

    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return m_valid;
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return m_path;
    }

  private:
    std::filesystem::path m_path;
    bool m_valid = false;
};

class FailingWorkspaceFilesystem final : public cue::FilesystemRoot
{
  public:
    explicit FailingWorkspaceFilesystem(std::unique_ptr<cue::FilesystemRoot> a_inner,
                                        const cue::AssertContext &a_assertContext) noexcept
        : m_inner(std::move(a_inner)), m_assertContext(&a_assertContext)
    {
    }

    void set_write_failure(bool a_shouldFail) noexcept
    {
        m_shouldFail = a_shouldFail;
    }

    [[nodiscard]] cue::Result<cue::EntryType> query_entry(const cue::RelativePath &a_path) noexcept override
    {
        return m_inner->query_entry(a_path);
    }

    [[nodiscard]] cue::Result<std::vector<std::byte>> read_file(const cue::RelativePath &a_path,
                                                                std::size_t a_maxBytes) noexcept override
    {
        return m_inner->read_file(a_path, a_maxBytes);
    }

    [[nodiscard]] cue::Result<void> create_directories(const cue::RelativePath &a_path) noexcept override
    {
        return m_inner->create_directories(a_path);
    }

    [[nodiscard]] cue::Result<void> write_file_atomic(const cue::RelativePath &a_path,
                                                      std::span<const std::byte> a_bytes) noexcept override
    {
        if (m_shouldFail)
        {
            return cue::Result<void>::failure(cue::project_hub::make_project_hub_error(
                *m_assertContext, cue::project_hub::ProjectHubError::PersistenceFailure,
                "Test workspace write failure"));
        }
        return m_inner->write_file_atomic(a_path, a_bytes);
    }

    [[nodiscard]] cue::Result<void> write_recovery_backup_atomic(
        const cue::RelativePath &a_destination, std::span<const std::byte> a_bytes,
        const cue::AssertContext &a_assertContext) noexcept override
    {
        return m_inner->write_recovery_backup_atomic(a_destination, a_bytes, a_assertContext);
    }

    [[nodiscard]] cue::Result<cue::FileWriteLease> acquire_file_write_lease(
        const cue::RelativePath &a_path) noexcept override
    {
        return m_inner->acquire_file_write_lease(a_path);
    }

    [[nodiscard]] cue::Result<void> write_file_atomic_if_unchanged(cue::FileWriteLease &a_lease,
                                                                   const cue::RelativePath &a_path,
                                                                   cue::FileFingerprint a_expected,
                                                                   std::size_t a_maximumExpectedBytes,
                                                                   std::span<const std::byte> a_bytes) noexcept override
    {
        return m_inner->write_file_atomic_if_unchanged(a_lease, a_path, a_expected, a_maximumExpectedBytes, a_bytes);
    }

    [[nodiscard]] cue::Result<void> remove_file(const cue::RelativePath &a_path) noexcept override
    {
        return m_inner->remove_file(a_path);
    }

    [[nodiscard]] cue::Result<cue::StagingArea> create_staging_area(
        const cue::RelativePath &a_destination) noexcept override
    {
        return m_inner->create_staging_area(a_destination);
    }

    [[nodiscard]] cue::Result<void> publish_staging_area(cue::StagingArea &&a_staging,
                                                         const cue::RelativePath &a_destination) noexcept override
    {
        return m_inner->publish_staging_area(std::move(a_staging), a_destination);
    }

    [[nodiscard]] cue::Result<void> rollback_staging_area(cue::StagingArea &&a_staging) noexcept override
    {
        return m_inner->rollback_staging_area(std::move(a_staging));
    }

  private:
    std::unique_ptr<cue::FilesystemRoot> m_inner;
    const cue::AssertContext *m_assertContext;
    bool m_shouldFail = false;
};

class TestProjectHubPlatform final : public cue::project_hub::ProjectHubPlatform
{
  public:
    explicit TestProjectHubPlatform(const cue::AssertContext &a_assertContext) noexcept
        : m_assertContext(&a_assertContext)
    {
    }

    [[nodiscard]] cue::Result<std::string> normalize_project_locator(std::string_view a_locator) noexcept override
    {
        try
        {
            if (a_locator.empty())
            {
                return cue::Result<std::string>::failure(cue::project_hub::make_project_hub_error(
                    *m_assertContext, cue::project_hub::ProjectHubError::InvalidLocator, "Locator is empty"));
            }
            std::filesystem::path path{std::string(a_locator)};
            path = std::filesystem::absolute(path).lexically_normal();
            std::string normalized = to_utf8(path);
            return cue::Result<std::string>::success(std::move(normalized));
        }
        catch (...)
        {
            m_assertContext->fatal_handler().terminate("Test locator normalization failed");
        }
        std::terminate();
    }

    [[nodiscard]] cue::Result<std::string> compose_project_locator(std::string_view a_parentLocator,
                                                                   std::string_view a_projectName) noexcept override
    {
        try
        {
            std::filesystem::path path{std::string(a_parentLocator)};
            path /= std::string(a_projectName);
            std::string composed = to_utf8(path.lexically_normal());
            return cue::Result<std::string>::success(std::move(composed));
        }
        catch (...)
        {
            m_assertContext->fatal_handler().terminate("Test project locator composition failed");
        }
        std::terminate();
    }

    [[nodiscard]] cue::Result<std::string> compose_descriptor_locator(
        std::string_view a_projectLocator) noexcept override
    {
        try
        {
            std::filesystem::path path{std::string(a_projectLocator)};
            path /= "CueProject.json";
            std::string composed = to_utf8(path.lexically_normal());
            return cue::Result<std::string>::success(std::move(composed));
        }
        catch (...)
        {
            m_assertContext->fatal_handler().terminate("Test descriptor locator composition failed");
        }
        std::terminate();
    }

    [[nodiscard]] cue::Result<std::unique_ptr<cue::FilesystemRoot>> open_root(
        std::string_view a_locator) noexcept override
    {
        try
        {
            const std::filesystem::path path{std::string(a_locator)};
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (error)
            {
                return cue::Result<std::unique_ptr<cue::FilesystemRoot>>::failure(
                    cue::project_hub::make_project_hub_error(*m_assertContext,
                                                             cue::project_hub::ProjectHubError::InvalidLocator,
                                                             "Locator existence could not be queried"));
            }
            if (!exists)
            {
                std::unique_ptr<cue::FilesystemRoot> missing;
                return cue::Result<std::unique_ptr<cue::FilesystemRoot>>::success(std::move(missing));
            }
            return cue::create_windows_filesystem_root(to_utf8(path), *m_assertContext);
        }
        catch (...)
        {
            m_assertContext->fatal_handler().terminate("Test project root open failed");
        }
        std::terminate();
    }

    [[nodiscard]] cue::Result<cue::ProjectId> next_project_id() noexcept override
    {
        constexpr std::string_view ids[] = {"00000000-0000-4000-8000-000000000001",
                                            "00000000-0000-4000-8000-000000000002",
                                            "00000000-0000-4000-8000-000000000003"};
        if (m_nextIdentity >= std::size(ids))
        {
            return cue::Result<cue::ProjectId>::failure(cue::project_hub::make_project_hub_error(
                *m_assertContext, cue::project_hub::ProjectHubError::InvalidConfiguration,
                "Test ProjectId source is exhausted"));
        }
        return cue::ProjectId::parse(ids[m_nextIdentity++], *m_assertContext);
    }

  private:
    const cue::AssertContext *m_assertContext;
    std::size_t m_nextIdentity = 0U;
};

[[nodiscard]] cue::Result<cue::project_hub::ProjectHubConfiguration> make_configuration(
    const cue::AssertContext &a_assertContext) noexcept
{
    auto profile = cue::ProjectCapabilityProfile::create({}, a_assertContext);
    auto snapshot = cue::ProjectCapabilitySnapshot::create({}, a_assertContext);
    if (!profile)
    {
        return cue::Result<cue::project_hub::ProjectHubConfiguration>::failure(std::move(*profile.try_error()));
    }
    if (!snapshot)
    {
        return cue::Result<cue::project_hub::ProjectHubConfiguration>::failure(std::move(*snapshot.try_error()));
    }
    cue::project_hub::ProjectHubConfiguration configuration{
        1U, cue::EngineVersion{1U, 0U, 0U}, std::move(*profile.try_value()), std::move(*snapshot.try_value()),
        cue::EngineCompatibility{cue::EngineVersion{1U, 0U, 0U}, cue::EngineVersion{2U, 0U, 0U}}};
    return cue::Result<cue::project_hub::ProjectHubConfiguration>::success(std::move(configuration));
}

[[nodiscard]] const cue::project_hub::ProjectRowView *find_project(
    std::span<const cue::project_hub::ProjectRowView> a_projects, std::string_view a_projectId) noexcept
{
    for (const cue::project_hub::ProjectRowView &project : a_projects)
    {
        if (project.projectId == a_projectId)
        {
            return &project;
        }
    }
    return nullptr;
}

[[nodiscard]] bool overwrite_descriptor(const std::filesystem::path &a_path, std::string_view a_text)
{
    std::ofstream stream(a_path, std::ios::binary | std::ios::trunc);
    stream.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
    return stream.good();
}

[[nodiscard]] bool test_project_hub_workflow(const cue::AssertContext &a_assertContext)
{
    TestDirectory directory;
    if (!directory.valid())
    {
        return false;
    }
    auto workspace = cue::create_windows_filesystem_root(to_utf8(directory.path() / "Workspace"), a_assertContext);
    auto configuration = make_configuration(a_assertContext);
    TestProjectHubPlatform platform(a_assertContext);
    if (!workspace || !configuration)
    {
        return false;
    }
    FailingWorkspaceFilesystem workspaceFilesystem(std::move(*workspace.try_value()), a_assertContext);
    auto service = cue::project_hub::ProjectHubService::create(workspaceFilesystem, platform,
                                                               std::move(*configuration.try_value()), a_assertContext);
    if (!service || service.try_value()->get()->templates().size() != 1U ||
        service.try_value()->get()->templates()[0].id != cue::project_hub::k_blank3dTemplateId ||
        !service.try_value()->get()->projects().empty())
    {
        return false;
    }

    const std::string projectsLocator = to_utf8(directory.path() / "Projects");
    if (service.try_value()->get()->create_blank_project(projectsLocator, "Invalid", "Invalid Project",
                                                         "unknown-template", 0U) ||
        !service.try_value()->get()->create_blank_project(projectsLocator, "Alpha", "Alpha Project",
                                                          cue::project_hub::k_blank3dTemplateId, 100U) ||
        !service.try_value()->get()->create_blank_project(projectsLocator, "Bravo", "Bravo Project",
                                                          cue::project_hub::k_blank3dTemplateId, 200U) ||
        !service.try_value()->get()->create_blank_project(projectsLocator, "Charlie", "Charlie Project",
                                                          cue::project_hub::k_blank3dTemplateId, 300U))
    {
        return false;
    }
    constexpr std::string_view alphaId = "00000000-0000-4000-8000-000000000001";
    constexpr std::string_view bravoId = "00000000-0000-4000-8000-000000000002";
    constexpr std::string_view charlieId = "00000000-0000-4000-8000-000000000003";
    const auto *alpha = find_project(service.try_value()->get()->projects(), alphaId);
    if (alpha == nullptr || alpha->state != cue::project_hub::ProjectEntryState::Available || !alpha->canOpen ||
        alpha->compatibilityStatus != cue::ProjectCompatibilityStatus::Compatible ||
        !alpha->engineCompatibility.has_value() ||
        alpha->engineCompatibility->minimum != cue::EngineVersion{1U, 0U, 0U})
    {
        return false;
    }

    if (!service.try_value()->get()->set_project_pinned(alphaId, true) ||
        !service.try_value()->get()->set_project_pinned(charlieId, true) ||
        !service.try_value()->get()->move_pinned_project(charlieId, 0U) ||
        service.try_value()->get()->projects()[0].projectId != charlieId)
    {
        return false;
    }

    auto invalidLaunch =
        service.try_value()->get()->open_project(alphaId, 350U, std::string_view("../Outside.cuescene"));
    const auto *alphaAfterInvalidLaunch = find_project(service.try_value()->get()->projects(), alphaId);
    if (invalidLaunch || alphaAfterInvalidLaunch == nullptr || alphaAfterInvalidLaunch->lastOpenedMilliseconds != 100U)
    {
        return false;
    }
    auto launch = service.try_value()->get()->open_project(alphaId, 400U, std::string_view("Scenes/Start.cuescene"));
    if (!launch || launch.try_value()->protocol_version() != cue::project_hub::k_editorLaunchProtocolVersion ||
        launch.try_value()->expected_project_id() != alphaId ||
        launch.try_value()->engine_compatibility_id() != "cue-engine:[1.0.0,2.0.0)" ||
        !launch.try_value()->initial_scene_locator().has_value() ||
        launch.try_value()->project_descriptor_locator().find("CueProject.json") == std::string_view::npos)
    {
        return false;
    }

    std::error_code error;
    std::filesystem::rename(directory.path() / "Projects" / "Bravo", directory.path() / "Projects" / "BravoMoved",
                            error);
    if (error)
    {
        return false;
    }
    auto missingLaunch = service.try_value()->get()->open_project(bravoId, 450U);
    if (missingLaunch)
    {
        return false;
    }
    const auto *missingBravo = find_project(service.try_value()->get()->projects(), bravoId);
    if (missingBravo == nullptr || missingBravo->state != cue::project_hub::ProjectEntryState::Missing ||
        !service.try_value()->get()->register_project(to_utf8(directory.path() / "Projects" / "BravoMoved"), 500U,
                                                      true))
    {
        return false;
    }
    const auto *movedBravo = find_project(service.try_value()->get()->projects(), bravoId);
    if (movedBravo == nullptr || movedBravo->state != cue::project_hub::ProjectEntryState::Moved)
    {
        return false;
    }

    if (!overwrite_descriptor(directory.path() / "Projects" / "Charlie" / "CueProject.json", "{broken") ||
        !service.try_value()->get()->refresh())
    {
        return false;
    }
    const auto *brokenCharlie = find_project(service.try_value()->get()->projects(), charlieId);
    if (brokenCharlie == nullptr || brokenCharlie->state != cue::project_hub::ProjectEntryState::Broken ||
        brokenCharlie->problem != cue::project_hub::ProjectEntryProblem::DescriptorInvalid)
    {
        return false;
    }

    workspaceFilesystem.set_write_failure(true);
    auto failedRemoval = service.try_value()->get()->remove_project(alphaId);
    workspaceFilesystem.set_write_failure(false);
    if (failedRemoval || find_project(service.try_value()->get()->projects(), alphaId) == nullptr)
    {
        return false;
    }
    if (!service.try_value()->get()->remove_project(alphaId) ||
        find_project(service.try_value()->get()->projects(), alphaId) != nullptr ||
        !std::filesystem::exists(directory.path() / "Projects" / "Alpha" / "CueProject.json"))
    {
        return false;
    }

    auto restartedConfiguration = make_configuration(a_assertContext);
    if (!restartedConfiguration)
    {
        return false;
    }
    auto restarted = cue::project_hub::ProjectHubService::create(
        workspaceFilesystem, platform, std::move(*restartedConfiguration.try_value()), a_assertContext);
    return restarted && restarted.try_value()->get()->projects().size() == 2U &&
           find_project(restarted.try_value()->get()->projects(), bravoId) != nullptr &&
           find_project(restarted.try_value()->get()->projects(), charlieId) != nullptr;
}
} // namespace

int main()
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    return test_project_hub_workflow(assertContext) ? 0 : 1;
}
