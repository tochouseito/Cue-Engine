#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/Project/Compatibility.h>
#include <Cue/Project/Generator.h>
#include <Cue/Project/Registry.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cue
{
class AssertContext;
class FilesystemRoot;
} // namespace cue

namespace cue::project_hub
{
inline constexpr std::uint32_t k_editorLaunchProtocolVersion = 1U;
inline constexpr std::string_view k_blank3dTemplateId = "cue.blank-3d";

/// @brief Stored Locator を開く Platform Composition 境界
class ProjectHubPlatform
{
  public:
    ProjectHubPlatform(const ProjectHubPlatform &) = delete;
    ProjectHubPlatform &operator=(const ProjectHubPlatform &) = delete;
    virtual ~ProjectHubPlatform() = default;

    /// @brief UI入力LocatorをProcess間受け渡し可能な絶対UTF-8 Locatorへ正規化する
    [[nodiscard]] virtual Result<std::string> normalize_project_locator(std::string_view a_locator) noexcept = 0;
    /// @brief 正規化済み親LocatorとProject名から正規化済みProject Locatorを作る
    [[nodiscard]] virtual Result<std::string> compose_project_locator(std::string_view a_parentLocator,
                                                                      std::string_view a_projectName) noexcept = 0;
    /// @brief Project Root LocatorからCueProject.jsonの正規化済みLocatorを作る
    [[nodiscard]] virtual Result<std::string> compose_descriptor_locator(
        std::string_view a_projectLocator) noexcept = 0;
    /// @brief LocatorのRootを開く。存在しない場合は成功したnullptr、その他の失敗はErrorを返す
    [[nodiscard]] virtual Result<std::unique_ptr<FilesystemRoot>> open_root(std::string_view a_locator) noexcept = 0;
    /// @brief 新規Project用のUUID Version 4を返す
    [[nodiscard]] virtual Result<ProjectId> next_project_id() noexcept = 0;

  protected:
    ProjectHubPlatform() noexcept = default;
};

/// @brief Project Hub Sessionに注入するEngineとCapabilityの所有設定
struct ProjectHubConfiguration final
{
    std::uint32_t supportedProjectFormatVersion;
    EngineVersion currentEngineVersion;
    ProjectCapabilityProfile capabilityProfile;
    ProjectCapabilitySnapshot capabilitySnapshot;
    EngineCompatibility blankProjectCompatibility;
};

/// @brief Project HubがUIへ提示する作成Template
struct ProjectTemplateView final
{
    std::string id;
    std::string displayName;
    EngineCompatibility engineCompatibility;
};

/// @brief Project一覧のDescriptorおよびLocator状態
enum class ProjectEntryState : std::uint8_t
{
    Available,
    Missing,
    Moved,
    Broken
};

/// @brief Broken状態をUI診断へ変換する安定分類
enum class ProjectEntryProblem : std::uint8_t
{
    None,
    LocatorAccessFailed,
    DescriptorInvalid,
    IdentityMismatch,
    CompatibilityInvalid
};

/// @brief UIがProject一覧を描画するための所有Snapshot Row
struct ProjectRowView final
{
    std::string projectId;
    std::string displayName;
    std::string locator;
    std::uint64_t lastOpenedMilliseconds;
    bool isPinned;
    ProjectEntryState state;
    ProjectEntryProblem problem;
    ProjectCompatibilityStatus compatibilityStatus;
    bool canOpen;
    std::optional<EngineCompatibility> engineCompatibility;
    std::vector<ProjectCompatibilityReason> compatibilityReasons;
};

/// @brief Blank Project生成後のRecent登録状態と復旧情報
class ProjectCreationOutcome final
{
  public:
    ProjectCreationOutcome() = delete;
    ProjectCreationOutcome(const ProjectCreationOutcome &) = delete;
    ProjectCreationOutcome &operator=(const ProjectCreationOutcome &) = delete;
    ProjectCreationOutcome(ProjectCreationOutcome &&) noexcept = default;
    ProjectCreationOutcome &operator=(ProjectCreationOutcome &&) noexcept = default;
    ~ProjectCreationOutcome() = default;

    /// @brief 生成ProjectがRecent Registryへ永続登録済みならtrueを返す
    [[nodiscard]] bool is_recent_registered() const noexcept;
    /// @brief 生成済みProjectの正規化Locatorを返す
    [[nodiscard]] std::string_view project_locator() const noexcept;
    /// @brief Project生成後のRecent登録失敗を返す。登録済みならnullptr
    [[nodiscard]] const Error *try_registration_error() const noexcept;

  private:
    friend class ProjectHubService;
    ProjectCreationOutcome(std::string &&a_projectLocator, std::optional<Error> &&a_registrationError) noexcept;

    std::string m_projectLocator;
    std::optional<Error> m_registrationError;
};

/// @brief Project HubからEditor Processへ値だけで渡すLaunch契約
class EditorLaunchRequest final
{
  public:
    EditorLaunchRequest() = delete;
    EditorLaunchRequest(const EditorLaunchRequest &) = delete;
    EditorLaunchRequest &operator=(const EditorLaunchRequest &) = delete;
    EditorLaunchRequest(EditorLaunchRequest &&) noexcept = default;
    EditorLaunchRequest &operator=(EditorLaunchRequest &&) noexcept = default;
    ~EditorLaunchRequest() = default;

    [[nodiscard]] std::uint32_t protocol_version() const noexcept;
    [[nodiscard]] std::string_view project_descriptor_locator() const noexcept;
    [[nodiscard]] std::string_view expected_project_id() const noexcept;
    [[nodiscard]] std::string_view engine_compatibility_id() const noexcept;
    [[nodiscard]] const std::optional<std::string> &initial_scene_locator() const noexcept;

  private:
    friend class ProjectHubService;
    EditorLaunchRequest(std::string &&a_projectDescriptorLocator, std::string &&a_expectedProjectId,
                        std::string &&a_engineCompatibilityId,
                        std::optional<std::string> &&a_initialSceneLocator) noexcept;

    std::string m_projectDescriptorLocator;
    std::string m_expectedProjectId;
    std::string m_engineCompatibilityId;
    std::optional<std::string> m_initialSceneLocator;
};

/// @brief Project Registry、Descriptor、Compatibilityを束ねるUI非依存Application Service
///
/// 同一Instanceは作成Threadだけで使用し、返すViewは次のMutationまで有効とする
/// createへ渡すWorkspace Filesystem、Platform、AssertContextは非所有で保持し、Serviceより長く生存させる
class ProjectHubService final
{
  public:
    ProjectHubService(const ProjectHubService &) = delete;
    ProjectHubService &operator=(const ProjectHubService &) = delete;
    ProjectHubService(ProjectHubService &&) = delete;
    ProjectHubService &operator=(ProjectHubService &&) = delete;
    ~ProjectHubService() = default;

    /// @brief Workspace Registryを読込み、初期ViewModelを構築する
    /// @param a_workspaceFilesystem 返却Serviceより長く生存する非所有Workspace Root
    /// @param a_platform 返却Serviceより長く生存する非所有Platform Composition
    /// @param a_configuration Serviceが所有権を取得するEngine・Capability設定
    /// @param a_assertContext 返却Serviceと注入Platformより長く生存する非所有診断Context
    [[nodiscard]] static Result<std::unique_ptr<ProjectHubService>> create(
        FilesystemRoot &a_workspaceFilesystem, ProjectHubPlatform &a_platform,
        ProjectHubConfiguration &&a_configuration, const AssertContext &a_assertContext) noexcept;

    [[nodiscard]] std::span<const ProjectTemplateView> templates() const noexcept;
    [[nodiscard]] std::span<const ProjectRowView> projects() const noexcept;

    /// @brief 全Recent Locatorを再検査し、欠損や破損をEntry単位で隔離してViewModelを更新する
    [[nodiscard]] Result<void> refresh() noexcept;
    /// @brief Blank TemplateでProjectをAtomic生成しRecentへ登録する
    ///
    /// Result失敗時はProjectが公開されていない。成功OutcomeでRecent未登録の場合もProject Folderは生成済みであり、
    /// project_locatorをregister_projectへ渡してRecent登録だけを再試行する
    [[nodiscard]] Result<ProjectCreationOutcome> create_blank_project(std::string_view a_parentLocator,
                                                                      std::string_view a_projectName,
                                                                      std::string_view a_displayName,
                                                                      std::string_view a_templateId,
                                                                      std::uint64_t a_openedMilliseconds) noexcept;
    /// @brief 既存Projectを登録し、明示時だけ同一ProjectIdの移動を再関連付けする
    [[nodiscard]] Result<void> register_project(std::string_view a_locator, std::uint64_t a_openedMilliseconds,
                                                bool a_confirmMovedProject) noexcept;
    /// @brief Descriptorを再検証し、互換ProjectのEditor Launch Requestを生成する
    [[nodiscard]] Result<EditorLaunchRequest> open_project(
        std::string_view a_projectId, std::uint64_t a_openedMilliseconds,
        std::optional<std::string_view> a_initialSceneLocator = std::nullopt) noexcept;
    /// @brief Recent EntryのPin状態を変更する
    [[nodiscard]] Result<void> set_project_pinned(std::string_view a_projectId, bool a_isPinned) noexcept;
    /// @brief Pin EntryをPin一覧内の位置へ移動する
    [[nodiscard]] Result<void> move_pinned_project(std::string_view a_projectId, std::size_t a_targetIndex) noexcept;
    /// @brief Recent Entryだけを除外しProject Folderには触れない
    [[nodiscard]] Result<void> remove_project(std::string_view a_projectId) noexcept;

  private:
    struct ConstructionKey final
    {
    };

    ProjectHubService(ConstructionKey, FilesystemRoot &a_workspaceFilesystem, ProjectHubPlatform &a_platform,
                      ProjectHubConfiguration &&a_configuration, RecentProjectRegistry &&a_registry,
                      const AssertContext &a_assertContext) noexcept;

    struct PreparedRegistrySnapshot final
    {
        std::vector<ProjectRowView> projects;
        bool registryChanged;
    };

    [[nodiscard]] Result<PreparedRegistrySnapshot> prepare_registry_snapshot(
        RecentProjectRegistry &a_registry) noexcept;
    [[nodiscard]] Result<RecentProjectRegistry> clone_registry() const noexcept;
    [[nodiscard]] Result<void> commit_registry(RecentProjectRegistry &&a_registry) noexcept;
    [[nodiscard]] Result<ProjectId> parse_project_id(std::string_view a_projectId) const noexcept;

    FilesystemRoot *m_workspaceFilesystem;
    ProjectHubPlatform *m_platform;
    const AssertContext *m_assertContext;
    ProjectHubConfiguration m_configuration;
    RecentProjectRegistry m_registry;
    std::vector<ProjectTemplateView> m_templates;
    std::vector<ProjectRowView> m_projects;
};
} // namespace cue::project_hub
