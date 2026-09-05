#include <Cue/Editor/Windows/EditorSession.h>

#include <Cue/EditorCore/EditorIntent.h>
#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Foundation/Windows/UtfConversion.h>
#include <Cue/IO/Windows/WindowsFilesystem.h>
#include <Cue/Project/Compatibility.h>
#include <Cue/Project/Generator.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>

namespace
{
/// @brief Test内のFatalを固定Exit Codeへ変換する
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief MessageなしFatalを固定Exit Codeへ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::_Exit(76);
    }

    /// @brief Message付きFatalを固定Exit Codeへ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::_Exit(76);
    }
};

/// @brief Process固有Temporary Project Directoryを一意所有する
class TestDirectory final
{
  public:
    /// @brief Temporary Root下へProcess固有Directoryを作成する
    TestDirectory()
    {
        m_path =
            std::filesystem::temp_directory_path() /
            (L"CueEditorWorkflow-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(m_path);
    }

    TestDirectory(const TestDirectory &) = delete;
    TestDirectory &operator=(const TestDirectory &) = delete;

    /// @brief Test所有Directoryだけを終了時に除去する
    ~TestDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    /// @brief Temporary RootのNative Pathを返す
    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return m_path;
    }

  private:
    std::filesystem::path m_path;
};

/// @brief Windows PathをStrict UTF-8へ変換する
[[nodiscard]] std::string to_utf8(const std::filesystem::path &a_path, cue::FatalHandler &a_handler)
{
    std::string converted;
    const cue::WindowsUtfConversionResult result =
        cue::convert_windows_utf16_to_utf8(a_path.native(), converted, a_handler);
    if (result.status != cue::WindowsUtfConversionStatus::Success)
    {
        std::_Exit(2);
    }
    return converted;
}

/// @brief M12 Editorが使用する現在Engine互換性入力を毎回生成する
[[nodiscard]] cue::editor::WindowsEditorEngineConfiguration make_configuration(const cue::AssertContext &a_context)
{
    auto profile = cue::ProjectCapabilityProfile::create({}, a_context);
    auto snapshot = cue::ProjectCapabilitySnapshot::create({}, a_context);
    if (!profile || !snapshot)
    {
        std::_Exit(3);
    }
    return {1U, cue::EngineVersion{1U, 0U, 0U}, std::move(*profile.try_value()), std::move(*snapshot.try_value())};
}

/// @brief 生成Projectへ対応するEditor起動値を作る
[[nodiscard]] cue::editor::WindowsEditorLaunchParameters make_parameters(
    const std::filesystem::path &a_projectPath, std::string_view a_projectId,
    const cue::EngineCompatibility &a_compatibility, const cue::AssertContext &a_context,
    std::optional<std::string> a_initialScene = std::nullopt)
{
    return {cue::k_editorLaunchProtocolVersion, to_utf8(a_projectPath / L"CueProject.json", a_context.fatal_handler()),
            std::string(a_projectId), cue::make_engine_compatibility_id(a_compatibility, a_context),
            std::move(a_initialScene)};
}

/// @brief Project生成からScene保存、Session再構築、Stable ID再Openまでを検証する
void test_process_round_trip(const cue::AssertContext &a_context)
{
    TestDirectory directory;
    auto parent = cue::create_windows_filesystem_root(to_utf8(directory.path(), a_context.fatal_handler()), a_context);
    auto projectId = cue::ProjectId::parse("00000000-0000-4000-8000-000000000901", a_context);
    const cue::EngineCompatibility engineCompatibility{cue::EngineVersion{1U, 0U, 0U}, cue::EngineVersion{2U, 0U, 0U}};
    if (!parent || !projectId)
    {
        std::_Exit(4);
    }
    auto generated = cue::generate_blank_project(**parent.try_value(), "WorkflowProject", "Workflow Project",
                                                 *projectId.try_value(), {engineCompatibility}, a_context);
    if (!generated)
    {
        std::_Exit(5);
    }

    const std::filesystem::path projectPath = directory.path() / L"WorkflowProject";
    auto session = cue::editor::WindowsEditorSession::create(
        make_parameters(projectPath, projectId.try_value()->text(), engineCompatibility, a_context),
        make_configuration(a_context), a_context);
    if (!session || (*session.try_value())->active_document_id().has_value())
    {
        std::_Exit(6);
    }
    auto sceneLocator = cue::RelativePath::parse("Scenes/Main.cuescene", a_context);
    if (!sceneLocator)
    {
        std::_Exit(7);
    }
    auto documentId = (*session.try_value())->create_scene(std::move(*sceneLocator.try_value()));
    if (!documentId)
    {
        std::_Exit(8);
    }
    cue::editor_core::AddObjectIntent addObject{std::nullopt, "Persistent Root"};
    auto edited = (*session.try_value())
                      ->controller()
                      .execute_intent(*documentId.try_value(), std::move(addObject),
                                      (*session.try_value())->identity_source(), {});
    if (!edited)
    {
        std::_Exit(9);
    }
    const cue::editor_core::EditorDocument *document =
        (*session.try_value())->controller().session().find_document(*documentId.try_value());
    if (document == nullptr || document->scene_document().object_count() != 1U)
    {
        std::_Exit(10);
    }
    const auto sceneText = document->scene_document().scene_asset_id().canonical_text();
    const auto objectText = document->scene_document().objects().front().id().canonical_text();
    auto saved = (*session.try_value())->save_active_scene();
    if (!saved || saved.try_value()->status() != cue::scene::SceneSaveStatus::Committed)
    {
        std::_Exit(11);
    }
    session.try_value()->reset();

    auto reopened = cue::editor::WindowsEditorSession::create(
        make_parameters(projectPath, projectId.try_value()->text(), engineCompatibility, a_context,
                        std::string("Scenes/Main.cuescene")),
        make_configuration(a_context), a_context);
    if (!reopened || !(*reopened.try_value())->active_document_id().has_value())
    {
        std::_Exit(12);
    }
    const cue::editor_core::EditorDocument *reopenedDocument =
        (*reopened.try_value())->controller().session().find_document(*(*reopened.try_value())->active_document_id());
    if (reopenedDocument == nullptr || reopenedDocument->scene_document().object_count() != 1U ||
        reopenedDocument->scene_document().scene_asset_id().canonical_text() != sceneText ||
        reopenedDocument->scene_document().objects().front().id().canonical_text() != objectText ||
        (*reopened.try_value())->controller().session().project_descriptor().project_id() != *projectId.try_value())
    {
        std::_Exit(13);
    }
    cue::editor_core::RenameObjectIntent renameObject{reopenedDocument->scene_document().objects().front().id(),
                                                       "Dirty Root"};
    auto renamed = (*reopened.try_value())
                       ->controller()
                       .execute_intent(*(*reopened.try_value())->active_document_id(), std::move(renameObject),
                                       (*reopened.try_value())->identity_source(), {});
    auto awaiting = (*reopened.try_value())->request_close();
    if (!renamed || !awaiting || *awaiting.try_value() != cue::editor_core::DocumentCloseState::AwaitingDecision)
    {
        std::_Exit(14);
    }
    auto cancelled = (*reopened.try_value())->respond_to_close(cue::editor_core::CloseDecision::Cancel);
    if (!cancelled || *cancelled.try_value() != cue::editor_core::DocumentCloseState::Open ||
        !(*reopened.try_value())->active_document_id().has_value())
    {
        std::_Exit(15);
    }
    awaiting = (*reopened.try_value())->request_close();
    auto discarded = (*reopened.try_value())->respond_to_close(cue::editor_core::CloseDecision::Discard);
    if (!awaiting || *awaiting.try_value() != cue::editor_core::DocumentCloseState::AwaitingDecision || !discarded ||
        *discarded.try_value() != cue::editor_core::DocumentCloseState::Closed ||
        (*reopened.try_value())->active_document_id().has_value())
    {
        std::_Exit(16);
    }

    auto wrongIdentity = cue::editor::WindowsEditorSession::create(
        make_parameters(projectPath, "00000000-0000-4000-8000-000000000902", engineCompatibility, a_context),
        make_configuration(a_context), a_context);
    if (wrongIdentity)
    {
        std::_Exit(17);
    }
    auto wrongCompatibilityParameters =
        make_parameters(projectPath, projectId.try_value()->text(), engineCompatibility, a_context);
    wrongCompatibilityParameters.engineCompatibilityId = "cue-engine:[9.0.0,)";
    auto wrongCompatibility = cue::editor::WindowsEditorSession::create(std::move(wrongCompatibilityParameters),
                                                                        make_configuration(a_context), a_context);
    if (wrongCompatibility)
    {
        std::_Exit(18);
    }
    auto invalidScene = cue::editor::WindowsEditorSession::create(
        make_parameters(projectPath, projectId.try_value()->text(), engineCompatibility, a_context,
                        std::string("../Outside.cuescene")),
        make_configuration(a_context), a_context);
    if (invalidScene)
    {
        std::_Exit(19);
    }
}
} // namespace

/// @brief Headless Editor制作Workflowを一Process内の再構築境界で検証する
int main()
{
    TestFatalHandler handler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(handler, std::move(sinks));
    cue::AssertContext context(logger, handler);
    test_process_round_trip(context);
    return 0;
}
