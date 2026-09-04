#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Foundation/Windows/UtfConversion.h>
#include <Cue/IO/RelativePath.h>
#include <Cue/IO/Windows/WindowsFilesystem.h>
#include <Cue/Project/Compatibility.h>
#include <Cue/ProjectHub/ImGui/ProjectHubPresenter.h>
#include <Cue/ProjectHub/Windows/WindowsProjectHubPlatform.h>
#include <Cue/ToolHost/WindowsD3D12/ToolHost.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Windows.h>

namespace
{
constexpr int k_initializationFailure = 1;
constexpr int k_toolHostFailure = 2;

/// @brief Project Hub PresenterをTool Host CallbackとEditor Process Adapterへ接続する
class ProjectHubToolClient final : public cue::tool_host::ToolHostClient
{
  public:
    /// @brief PresenterとEditor実行File LocatorをClient全寿命へ関連付ける
    ProjectHubToolClient(cue::project_hub::ProjectHubPresenter &a_presenter, std::string &&a_editorExecutableLocator,
                         const cue::AssertContext &a_assertContext) noexcept
        : m_presenter(&a_presenter), m_assertContext(&a_assertContext),
          m_editorExecutableLocator(std::move(a_editorExecutableLocator))
    {
    }

    /// @brief Presentation Callback Stateの複製を禁止する
    ProjectHubToolClient(const ProjectHubToolClient &) = delete;
    /// @brief Presentation Callback Stateの複製を禁止する
    ProjectHubToolClient &operator=(const ProjectHubToolClient &) = delete;
    /// @brief Presentationと診断Contextの非所有参照だけを破棄する
    ~ProjectHubToolClient() override = default;

    /// @brief Project Hub画面を描画し、生成されたLaunch RequestをWindows Adapterへ渡す
    void draw_frame() noexcept override
    {
        m_presenter->draw();
        std::optional<cue::project_hub::EditorLaunchRequest> request = m_presenter->take_editor_launch_request();
        if (!request.has_value())
        {
            return;
        }
        cue::Result<void> launched =
            cue::project_hub::launch_windows_editor_process(m_editorExecutableLocator, *request, *m_assertContext);
        if (!launched)
        {
            m_presenter->report_editor_launch_failure(*launched.try_error());
            return;
        }
        m_didLaunchEditor = true;
    }

    /// @brief Escape終了またはEditor起動成功後にTool Host終了を要求する
    [[nodiscard]] bool should_close() const noexcept override
    {
        return m_didLaunchEditor || m_presenter->is_exit_requested();
    }

  private:
    cue::project_hub::ProjectHubPresenter *m_presenter;
    const cue::AssertContext *m_assertContext;
    std::string m_editorExecutableLocator;
    bool m_didLaunchEditor = false;
};

/// @brief 実行中Project Hubと同じDirectoryにあるCueEditorTool.exeをUTF-8 Locatorで返す
[[nodiscard]] cue::Result<std::string> locate_editor_executable(const cue::AssertContext &a_assertContext) noexcept
{
    std::wstring modulePath(32768, L'\0');
    const DWORD capacity = static_cast<DWORD>(modulePath.size());
    const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), capacity);
    if (length == 0 || length >= capacity)
    {
        const DWORD nativeCode = length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.ProjectHubTool", 1);
        cue::NativeError native =
            cue::NativeError::create(a_assertContext.fatal_handler(), "Win32", static_cast<std::int64_t>(nativeCode));
        return cue::Result<std::string>::failure(cue::Error::create(a_assertContext.fatal_handler(), std::move(code),
                                                                    "Project Hub executable path could not be read",
                                                                    std::move(native)));
    }
    modulePath.resize(length);
    const std::size_t separator = modulePath.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.ProjectHubTool", 2);
        return cue::Result<std::string>::failure(cue::Error::create(a_assertContext.fatal_handler(), std::move(code),
                                                                    "Project Hub executable directory is invalid"));
    }
    modulePath.resize(separator + 1);
    try
    {
        modulePath.append(L"CueEditorTool.exe");
    }
    catch (...)
    {
        a_assertContext.fatal_handler().terminate("Project Hub editor executable path allocation failed");
    }

    std::string locator;
    const cue::WindowsUtfConversionResult conversion =
        cue::convert_windows_utf16_to_utf8(modulePath, locator, a_assertContext.fatal_handler());
    if (conversion.status != cue::WindowsUtfConversionStatus::Success)
    {
        cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.ProjectHubTool", 3);
        cue::NativeError native =
            cue::NativeError::create(a_assertContext.fatal_handler(), "Win32", conversion.nativeCode);
        return cue::Result<std::string>::failure(cue::Error::create(a_assertContext.fatal_handler(), std::move(code),
                                                                    "Editor executable path conversion failed",
                                                                    std::move(native)));
    }
    return cue::Result<std::string>::success(std::move(locator));
}

/// @brief M12 Project Hubが生成・受理するProject VersionとCapability条件を構築する
[[nodiscard]] cue::Result<cue::project_hub::ProjectHubConfiguration> make_configuration(
    const cue::AssertContext &a_assertContext) noexcept
{
    cue::Result<cue::ProjectCapabilityProfile> profile = cue::ProjectCapabilityProfile::create({}, a_assertContext);
    if (!profile)
    {
        return cue::Result<cue::project_hub::ProjectHubConfiguration>::failure(std::move(*profile.try_error()));
    }
    cue::Result<cue::ProjectCapabilitySnapshot> snapshot = cue::ProjectCapabilitySnapshot::create({}, a_assertContext);
    if (!snapshot)
    {
        return cue::Result<cue::project_hub::ProjectHubConfiguration>::failure(std::move(*snapshot.try_error()));
    }
    cue::project_hub::ProjectHubConfiguration configuration{
        1U, cue::EngineVersion{1U, 0U, 0U}, std::move(*profile.try_value()), std::move(*snapshot.try_value()),
        cue::EngineCompatibility{cue::EngineVersion{1U, 0U, 0U}, cue::EngineVersion{2U, 0U, 0U}}};
    return cue::Result<cue::project_hub::ProjectHubConfiguration>::success(std::move(configuration));
}

/// @brief Project Hub ToolのDependency Graphを構築してUI Sessionを実行する
[[nodiscard]] int run(cue::Logger &a_logger, const cue::AssertContext &a_assertContext)
{
    cue::Result<cue::RelativePath> workspacePath = cue::RelativePath::parse("CueEngine/Workspace", a_assertContext);
    if (!workspacePath)
    {
        static_cast<void>(a_logger.log(cue::LogLevel::Error, "Workspace path initialization failed",
                                       std::move(*workspacePath.try_error())));
        return k_initializationFailure;
    }
    cue::Result<std::unique_ptr<cue::FilesystemRoot>> workspace = cue::create_windows_known_folder_filesystem_root(
        cue::WindowsKnownFolder::LocalApplicationData, *workspacePath.try_value(),
        cue::WindowsRootOpenMode::CreateOrOpen, a_assertContext);
    if (!workspace)
    {
        static_cast<void>(a_logger.log(cue::LogLevel::Error, "Workspace root initialization failed",
                                       std::move(*workspace.try_error())));
        return k_initializationFailure;
    }
    cue::Result<std::unique_ptr<cue::project_hub::ProjectHubPlatform>> platform =
        cue::project_hub::create_windows_project_hub_platform(a_assertContext);
    cue::Result<cue::project_hub::ProjectHubConfiguration> configuration = make_configuration(a_assertContext);
    if (!platform || !configuration)
    {
        cue::Error error = !platform ? std::move(*platform.try_error()) : std::move(*configuration.try_error());
        static_cast<void>(a_logger.log(cue::LogLevel::Error, "Project Hub configuration failed", std::move(error)));
        return k_initializationFailure;
    }
    cue::Result<std::unique_ptr<cue::project_hub::ProjectHubService>> service =
        cue::project_hub::ProjectHubService::create(**workspace.try_value(), **platform.try_value(),
                                                    std::move(*configuration.try_value()), a_assertContext);
    if (!service)
    {
        static_cast<void>(a_logger.log(cue::LogLevel::Error, "Project Hub service initialization failed",
                                       std::move(*service.try_error())));
        return k_initializationFailure;
    }
    cue::Result<std::unique_ptr<cue::project_hub::ProjectHubPresenter>> presenter =
        cue::project_hub::ProjectHubPresenter::create(**service.try_value(), a_assertContext);
    cue::Result<std::string> editorExecutable = locate_editor_executable(a_assertContext);
    if (!presenter || !editorExecutable)
    {
        cue::Error error = !presenter ? std::move(*presenter.try_error()) : std::move(*editorExecutable.try_error());
        static_cast<void>(
            a_logger.log(cue::LogLevel::Error, "Project Hub presentation initialization failed", std::move(error)));
        return k_initializationFailure;
    }

    ProjectHubToolClient client(**presenter.try_value(), std::move(*editorExecutable.try_value()), a_assertContext);
    const cue::tool_host::ToolHostDescriptor descriptor{"CueEngine Project Hub", {1280U, 720U}, 0U};
    cue::Result<void> hosted = cue::tool_host::run_windows_d3d12_tool_host(descriptor, client, a_assertContext);
    if (!hosted)
    {
        static_cast<void>(
            a_logger.log(cue::LogLevel::Error, "Project Hub Tool Host failed", std::move(*hosted.try_error())));
        return k_toolHostFailure;
    }
    return 0;
}
} // namespace

/// @brief Project Hub Toolの診断寿命を最外側で所有してUI Sessionの終了Codeを返す
int wmain()
{
    cue::AbortFatalHandler fatalHandler;
    try
    {
        std::vector<std::unique_ptr<cue::LogSink>> sinks;
        sinks.push_back(std::make_unique<cue::ConsoleLogSink>());
        cue::Logger logger(fatalHandler, std::move(sinks));
        cue::AssertContext assertContext(logger, fatalHandler);
        return run(logger, assertContext);
    }
    catch (...)
    {
        fatalHandler.terminate("Project Hub Tool allocation failed");
    }
}
