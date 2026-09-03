#include <Cue/EditorCore/EditorController.h>
#include <Cue/EditorCore/Error.h>
#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Math/Transform.h>
#include <Cue/Project/Descriptor.h>
#include <Cue/Scene/Identity.h>
#include <Cue/Scene/SceneDocument.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief Test中の通常FatalをProcess失敗へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::abort();
    }

    /// @brief Test中の予期しないFatalをProcess失敗へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

/// @brief 条件が偽ならTest Processを失敗終了する
void require(bool a_condition) noexcept
{
    if (!a_condition)
    {
        std::abort();
    }
}

/// @brief Errorが指定した診断Contextを含むか判定する
[[nodiscard]] bool has_error_context(const cue::Error &a_error, std::string_view a_expected) noexcept
{
    for (const cue::ErrorContext &context : a_error.contexts())
    {
        if (context.message() == a_expected)
        {
            return true;
        }
    }
    return false;
}

/// @brief 成功Resultから所有Valueを取り出す
template <typename T> T take_value(cue::Result<T> &&a_result) noexcept
{
    require(a_result.has_value());
    return std::move(*a_result.try_value());
}

/// @brief 固定IdentityからProject Descriptorを生成する
cue::ProjectDescriptor make_project_descriptor(const cue::AssertContext &a_assertContext) noexcept
{
    auto projectId = take_value(cue::ProjectId::parse("00000000-0000-4000-8000-000000000001", a_assertContext));
    return take_value(cue::create_blank_project_descriptor(
        projectId, "Editor Core Test", cue::EngineCompatibility{cue::EngineVersion{1U, 0U, 0U}, std::nullopt},
        a_assertContext));
}

/// @brief 固定Identityから空Scene Documentを生成する
cue::scene::SceneDocument make_scene_document(std::string_view a_sceneId,
                                              const cue::AssertContext &a_assertContext) noexcept
{
    auto sceneId = take_value(cue::scene::SceneAssetId::parse(a_sceneId, a_assertContext));
    return cue::scene::SceneDocument::create(std::move(sceneId), a_assertContext);
}

/// @brief 固定IdentityからObject IDを生成する
cue::scene::ObjectId make_object_id(std::string_view a_objectId, const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::scene::ObjectId::parse(a_objectId, a_assertContext));
}

/// @brief Project SessionとScene Openの一意性を検証する
void test_workspace_and_open() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    auto controller = cue::editor_core::EditorController::create(make_project_descriptor(assertContext), assertContext);

    auto scene = make_scene_document("00000000-0000-4000-8000-000000000101", assertContext);
    auto locator = take_value(cue::RelativePath::parse("Scenes/Main.cuescene", assertContext));
    const auto documentId = take_value(controller->open_document(std::move(scene), std::move(locator), true));

    require(controller->session().project_descriptor().display_name() == "Editor Core Test");
    require(controller->session().documents().size() == 1U);
    const auto *document = controller->session().find_document(documentId);
    require(document != nullptr);
    require(document->scene_locator().text() == "Scenes/Main.cuescene");
    require(document->has_saved_destination());

    auto duplicateScene = make_scene_document("00000000-0000-4000-8000-000000000101", assertContext);
    auto otherLocator = take_value(cue::RelativePath::parse("Scenes/Other.cuescene", assertContext));
    const auto duplicateSceneResult =
        controller->open_document(std::move(duplicateScene), std::move(otherLocator), true);
    require(!duplicateSceneResult.has_value());
    require(duplicateSceneResult.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::editor_core::EditorCoreError::DuplicateScene));

    auto otherScene = make_scene_document("00000000-0000-4000-8000-000000000102", assertContext);
    auto duplicateLocator = take_value(cue::RelativePath::parse("scenes/main.cuescene", assertContext));
    const auto duplicateLocatorResult =
        controller->open_document(std::move(otherScene), std::move(duplicateLocator), true);
    require(!duplicateLocatorResult.has_value());
    require(duplicateLocatorResult.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::editor_core::EditorCoreError::DuplicateLocator));
}

/// @brief DirtyがRevision差だけから一貫して決まることを検証する
void test_revision_and_dirty() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    auto controller = cue::editor_core::EditorController::create(make_project_descriptor(assertContext), assertContext);
    auto scene = make_scene_document("00000000-0000-4000-8000-000000000201", assertContext);
    auto locator = take_value(cue::RelativePath::parse("Scenes/Revision.cuescene", assertContext));
    const auto documentId = take_value(controller->open_document(std::move(scene), std::move(locator), true));
    const auto *document = controller->session().find_document(documentId);

    require(document != nullptr);
    require(document->current_state_id().value() == 1U);
    require(document->saved_state_id().value() == 1U);
    require(!document->is_dirty());

    const auto secondState = take_value(controller->record_persistent_change(documentId));
    require(secondState.value() == 2U);
    document = controller->session().find_document(documentId);
    require(document != nullptr);
    require(document->is_dirty());
    require(controller->mark_saved(documentId, secondState).has_value());
    document = controller->session().find_document(documentId);
    require(document != nullptr);
    require(!document->is_dirty());

    const auto thirdState = take_value(controller->record_persistent_change(documentId));
    require(thirdState.value() == 3U);
    document = controller->session().find_document(documentId);
    require(document != nullptr);
    require(controller->mark_saved(documentId, secondState).has_value());
    document = controller->session().find_document(documentId);
    require(document != nullptr);
    require(document->is_dirty());

    const auto missingDocument = controller->mark_saved(cue::editor_core::EditorDocumentId(999U), secondState);
    require(!missingDocument.has_value());
    require(missingDocument.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::editor_core::EditorCoreError::DocumentNotFound));
    require(has_error_context(*missingDocument.try_error(), "EditorDocumentId=999"));

    auto secondScene = make_scene_document("00000000-0000-4000-8000-000000000202", assertContext);
    auto secondLocator = take_value(cue::RelativePath::parse("Scenes/SecondRevision.cuescene", assertContext));
    const auto secondDocumentId =
        take_value(controller->open_document(std::move(secondScene), std::move(secondLocator), true));
    const auto secondDocumentState = take_value(controller->record_persistent_change(secondDocumentId));
    require(secondDocumentState.value() == secondState.value());
    require(secondDocumentState.document_id() == secondDocumentId);

    const auto crossDocumentSave = controller->mark_saved(secondDocumentId, secondState);
    require(!crossDocumentSave.has_value());
    require(crossDocumentSave.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::editor_core::EditorCoreError::InvalidSavedState));
    require(has_error_context(*crossDocumentSave.try_error(), "EditorDocumentId=2"));

    auto secondController =
        cue::editor_core::EditorController::create(make_project_descriptor(assertContext), assertContext);
    auto nextSessionScene = make_scene_document("00000000-0000-4000-8000-000000000203", assertContext);
    auto nextSessionLocator =
        take_value(cue::RelativePath::parse("Scenes/NextSessionRevision.cuescene", assertContext));
    const auto nextSessionDocumentId =
        take_value(secondController->open_document(std::move(nextSessionScene), std::move(nextSessionLocator), true));
    const auto nextSessionState = take_value(secondController->record_persistent_change(nextSessionDocumentId));
    require(nextSessionDocumentId == documentId);
    require(nextSessionState.value() == secondState.value());

    const auto crossSessionSave = secondController->mark_saved(nextSessionDocumentId, secondState);
    require(!crossSessionSave.has_value());
    require(crossSessionSave.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::editor_core::EditorCoreError::InvalidSavedState));
    require(has_error_context(*crossSessionSave.try_error(), "EditorDocumentId=1"));
}

/// @brief SelectionがStable ObjectIdだけを順序付き集合として保持することを検証する
void test_selection_reconciliation() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    auto controller = cue::editor_core::EditorController::create(make_project_descriptor(assertContext), assertContext);
    auto scene = make_scene_document("00000000-0000-4000-8000-000000000301", assertContext);
    const auto first = make_object_id("00000000-0000-4000-8000-000000000311", assertContext);
    const auto second = make_object_id("00000000-0000-4000-8000-000000000312", assertContext);
    const auto removed = make_object_id("00000000-0000-4000-8000-000000000313", assertContext);
    require(scene.add_object(first, "First", true, std::nullopt, cue::math::Transform{}).has_value());
    require(scene.add_object(second, "Second", true, std::nullopt, cue::math::Transform{}).has_value());
    auto locator = take_value(cue::RelativePath::parse("Scenes/Selection.cuescene", assertContext));
    const auto documentId = take_value(controller->open_document(std::move(scene), std::move(locator), true));

    const std::array selection{first, removed, first, second};
    require(controller->set_selection(documentId, selection, &removed).has_value());
    const auto *document = controller->session().find_document(documentId);
    require(document != nullptr);
    require(document->selection().size() == 2U);
    require(document->selection()[0] == first);
    require(document->selection()[1] == second);
    require(document->try_primary_selection() != nullptr);
    require(*document->try_primary_selection() == first);

    const std::array staleSelection{removed};
    require(controller->set_selection(documentId, staleSelection, &removed).has_value());
    document = controller->session().find_document(documentId);
    require(document != nullptr);
    require(document->selection().empty());
    require(document->try_primary_selection() == nullptr);
}

/// @brief 外部変更とClose判断の状態遷移を検証する
void test_external_change_and_close() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    auto controller = cue::editor_core::EditorController::create(make_project_descriptor(assertContext), assertContext);

    auto cleanScene = make_scene_document("00000000-0000-4000-8000-000000000401", assertContext);
    auto cleanLocator = take_value(cue::RelativePath::parse("Scenes/Clean.cuescene", assertContext));
    const auto cleanId = take_value(controller->open_document(std::move(cleanScene), std::move(cleanLocator), true));
    require(take_value(controller->request_close(cleanId)) == cue::editor_core::DocumentCloseState::Closed);
    require(controller->session().find_document(cleanId) == nullptr);

    auto unsavedScene = make_scene_document("00000000-0000-4000-8000-000000000402", assertContext);
    auto unsavedLocator = take_value(cue::RelativePath::parse("Scenes/Unsaved.cuescene", assertContext));
    const auto unsavedId =
        take_value(controller->open_document(std::move(unsavedScene), std::move(unsavedLocator), false));
    const auto *unsavedDocument = controller->session().find_document(unsavedId);
    require(unsavedDocument != nullptr);
    const auto unsavedState = unsavedDocument->current_state_id();
    require(take_value(controller->request_close(unsavedId)) == cue::editor_core::DocumentCloseState::AwaitingDecision);
    require(take_value(controller->respond_to_close(unsavedId, cue::editor_core::CloseDecision::Cancel)) ==
            cue::editor_core::DocumentCloseState::Open);
    require(take_value(controller->request_close(unsavedId)) == cue::editor_core::DocumentCloseState::AwaitingDecision);
    require(take_value(controller->respond_to_close(unsavedId, cue::editor_core::CloseDecision::Save)) ==
            cue::editor_core::DocumentCloseState::SaveRequested);
    require(controller->mark_saved(unsavedId, unsavedState).has_value());
    require(controller->session().find_document(unsavedId) == nullptr);

    auto advancedScene = make_scene_document("00000000-0000-4000-8000-000000000405", assertContext);
    auto advancedLocator = take_value(cue::RelativePath::parse("Scenes/Advanced.cuescene", assertContext));
    const auto advancedId =
        take_value(controller->open_document(std::move(advancedScene), std::move(advancedLocator), true));
    const auto *advancedDocument = controller->session().find_document(advancedId);
    require(advancedDocument != nullptr);
    const auto initialAdvancedState = advancedDocument->current_state_id();
    require(controller->record_persistent_change(advancedId).has_value());
    require(take_value(controller->request_close(advancedId)) ==
            cue::editor_core::DocumentCloseState::AwaitingDecision);
    require(take_value(controller->respond_to_close(advancedId, cue::editor_core::CloseDecision::Save)) ==
            cue::editor_core::DocumentCloseState::SaveRequested);
    require(controller->mark_saved(advancedId, initialAdvancedState).has_value());
    advancedDocument = controller->session().find_document(advancedId);
    require(advancedDocument != nullptr);
    require(advancedDocument->is_dirty());
    require(advancedDocument->close_state() == cue::editor_core::DocumentCloseState::AwaitingDecision);

    auto discardScene = make_scene_document("00000000-0000-4000-8000-000000000403", assertContext);
    auto discardLocator = take_value(cue::RelativePath::parse("Scenes/Discard.cuescene", assertContext));
    const auto discardId =
        take_value(controller->open_document(std::move(discardScene), std::move(discardLocator), false));
    require(take_value(controller->request_close(discardId)) == cue::editor_core::DocumentCloseState::AwaitingDecision);
    require(take_value(controller->respond_to_close(discardId, cue::editor_core::CloseDecision::Discard)) ==
            cue::editor_core::DocumentCloseState::Closed);
    require(controller->session().find_document(discardId) == nullptr);

    auto changedScene = make_scene_document("00000000-0000-4000-8000-000000000404", assertContext);
    auto changedLocator = take_value(cue::RelativePath::parse("Scenes/Changed.cuescene", assertContext));
    const auto changedId =
        take_value(controller->open_document(std::move(changedScene), std::move(changedLocator), true));
    require(
        controller->set_external_change_state(changedId, cue::editor_core::ExternalChangeState::Modified).has_value());
    require(take_value(controller->request_close(changedId)) == cue::editor_core::DocumentCloseState::AwaitingDecision);
}
} // namespace

int main()
{
    test_workspace_and_open();
    test_revision_and_dirty();
    test_selection_reconciliation();
    test_external_change_and_close();
    return 0;
}
