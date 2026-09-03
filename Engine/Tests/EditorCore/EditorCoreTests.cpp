#include <Cue/EditorCore/EditorController.h>
#include <Cue/EditorCore/Error.h>
#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Math/Transform.h>
#include <Cue/Project/Descriptor.h>
#include <Cue/Scene/Error.h>
#include <Cue/Scene/Identity.h>
#include <Cue/Scene/SceneDocument.h>
#include <Cue/Schema/Descriptor.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
class TestFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief Test 中の通常 Fatal を Process 失敗へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::abort();
    }

    /// @brief Test 中の予期しない Fatal を Process 失敗へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

/// @brief 条件が偽なら Test Process を失敗終了する
void require(bool a_condition) noexcept
{
    if (!a_condition)
    {
        std::abort();
    }
}

/// @brief Error が指定した診断 Context を含むか判定する
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

/// @brief 成功 Result から所有 Value を取り出す
template <typename T> T take_value(cue::Result<T> &&a_result) noexcept
{
    require(a_result.has_value());
    return std::move(*a_result.try_value());
}

/// @brief 固定 Identity から Project Descriptor を生成する
cue::ProjectDescriptor make_project_descriptor(const cue::AssertContext &a_assertContext) noexcept
{
    auto projectId = take_value(cue::ProjectId::parse("00000000-0000-4000-8000-000000000001", a_assertContext));
    return take_value(cue::create_blank_project_descriptor(
        projectId, "Editor Core Test", cue::EngineCompatibility{cue::EngineVersion{1U, 0U, 0U}, std::nullopt},
        a_assertContext));
}

/// @brief 固定 Identity から空 Scene Document を生成する
cue::scene::SceneDocument make_scene_document(std::string_view a_sceneId,
                                              const cue::AssertContext &a_assertContext) noexcept
{
    auto sceneId = take_value(cue::scene::SceneAssetId::parse(a_sceneId, a_assertContext));
    return cue::scene::SceneDocument::create(std::move(sceneId), a_assertContext);
}

/// @brief 固定 Identity から Object ID を生成する
cue::scene::ObjectId make_object_id(std::string_view a_objectId, const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::scene::ObjectId::parse(a_objectId, a_assertContext));
}

/// @brief 固定 Identity から Component Instance ID を生成する
cue::scene::ComponentInstanceId make_component_id(std::string_view a_componentId,
                                                  const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::scene::ComponentInstanceId::parse(a_componentId, a_assertContext));
}

/// @brief Scene Command Test 用 Stable Component Type Identity を生成する
cue::schema::TypeId make_component_type_id(const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::schema::TypeId::parse("10000000-0000-4000-8000-000000000001", a_assertContext));
}

/// @brief Scene Command Test 用 Stable Field Identity を生成する
cue::schema::FieldId make_health_field_id(const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::schema::FieldId::create(1U, a_assertContext));
}

/// @brief Scene Command Test 用 Asset Reference Field Identity を生成する
cue::schema::FieldId make_asset_field_id(const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::schema::FieldId::create(2U, a_assertContext));
}

/// @brief Scene Command Test 用 Schema Version を生成する
cue::schema::SchemaVersion make_component_version(const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::schema::SchemaVersion::create(1U, a_assertContext));
}

/// @brief Health Field を持つ Scene Command Test 用 Schema Registry を構築する
std::unique_ptr<cue::schema::SchemaRegistry> make_component_registry(
    cue::schema::SchemaRegistryIdentitySource &a_identitySource, const cue::AssertContext &a_assertContext) noexcept
{
    std::vector<cue::schema::FieldDescriptor> fields;
    fields.push_back(take_value(
        cue::schema::create_field_descriptor(make_health_field_id(a_assertContext), "health", a_assertContext)));
    fields.push_back(take_value(
        cue::schema::create_field_descriptor(make_asset_field_id(a_assertContext), "asset", a_assertContext)));
    std::vector<cue::schema::FieldId> reserved;
    auto descriptor = take_value(cue::schema::create_type_descriptor(
        make_component_type_id(a_assertContext), "Cue.EditorCore.TestComponent",
        make_component_version(a_assertContext), std::move(fields), std::move(reserved), a_assertContext));
    cue::schema::SchemaRegistryBuilder builder(a_identitySource, a_assertContext);
    require(builder.add_type(std::move(descriptor)).has_value());
    return take_value(builder.seal());
}

/// @brief Health Field の値 Kind を固定する Scene Component Value Registry を構築する
cue::scene::ComponentValueSchemaRegistry make_component_value_registry(
    const cue::schema::SchemaRegistry &a_registry, const cue::AssertContext &a_assertContext) noexcept
{
    std::vector<cue::scene::FieldKindBinding> bindings{
        {make_health_field_id(a_assertContext), cue::scene::FieldValueKind::SignedInteger},
        {make_asset_field_id(a_assertContext), cue::scene::FieldValueKind::AssetReference}};
    std::vector<cue::scene::ComponentValueSchema> schemas;
    schemas.push_back(take_value(cue::scene::create_component_value_schema(
        make_component_type_id(a_assertContext), make_component_version(a_assertContext), std::move(bindings),
        a_registry, a_assertContext)));
    return take_value(
        cue::scene::ComponentValueSchemaRegistry::create(std::move(schemas), a_registry, a_assertContext));
}

/// @brief 指定 Stable Identity と Health 値を持つ既知 Component を生成する
cue::scene::SceneComponent make_health_component(cue::scene::ComponentInstanceId a_componentId, std::int64_t a_health,
                                                 const cue::schema::SchemaRegistry &a_registry,
                                                 const cue::scene::ComponentValueSchemaRegistry &a_valueRegistry,
                                                 const cue::AssertContext &a_assertContext) noexcept
{
    std::vector<cue::scene::KnownFieldData> fields;
    fields.push_back(take_value(cue::scene::create_known_field(
        make_health_field_id(a_assertContext), cue::scene::FieldValue::signed_integer(a_health),
        cue::scene::FieldValueKind::SignedInteger, a_assertContext)));
    auto assetReference = take_value(cue::scene::AssetReferenceValue::create("asset://default", a_assertContext));
    fields.push_back(take_value(cue::scene::create_known_field(
        make_asset_field_id(a_assertContext), cue::scene::FieldValue::asset_reference(std::move(assetReference)),
        cue::scene::FieldValueKind::AssetReference, a_assertContext)));
    std::vector<cue::scene::OpaqueFieldData> unknownFields;
    auto component = take_value(cue::scene::create_known_component(
        std::move(a_componentId), make_component_type_id(a_assertContext), make_component_version(a_assertContext),
        std::move(fields), std::move(unknownFields), a_registry, a_valueRegistry, a_assertContext));
    return cue::scene::SceneComponent::known(std::move(component));
}

/// @brief Object 内の指定 Component から Health 値を取得する
std::int64_t component_health(const cue::scene::SceneObject &a_object,
                              const cue::scene::ComponentInstanceId &a_componentId) noexcept
{
    for (const cue::scene::SceneComponent &component : a_object.components())
    {
        if (component.instance_id() != a_componentId)
        {
            continue;
        }
        const cue::scene::KnownComponentData *known = component.try_known();
        require(known != nullptr);
        for (const cue::scene::KnownFieldData &field : known->known_fields())
        {
            if (field.id().value() == 1U)
            {
                const std::int64_t *value = field.value().try_signed_integer();
                require(value != nullptr);
                return *value;
            }
        }
    }
    std::abort();
}

/// @brief Object 内の指定 Component から Asset Reference Token を取得する
std::string_view component_asset_token(const cue::scene::SceneObject &a_object,
                                       const cue::scene::ComponentInstanceId &a_componentId) noexcept
{
    for (const cue::scene::SceneComponent &component : a_object.components())
    {
        if (component.instance_id() != a_componentId)
        {
            continue;
        }
        const cue::scene::KnownComponentData *known = component.try_known();
        require(known != nullptr);
        for (const cue::scene::KnownFieldData &field : known->known_fields())
        {
            if (field.id().value() == 2U)
            {
                const cue::scene::AssetReferenceValue *value = field.value().try_asset_reference();
                require(value != nullptr);
                return value->token();
            }
        }
    }
    std::abort();
}

/// @brief Project Session と Scene Open の一意性を検証する
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
    require(has_error_context(*duplicateSceneResult.try_error(), "ConflictingEditorDocumentId=1"));
    require(has_error_context(*duplicateSceneResult.try_error(),
                              "RequestedSceneAssetId=00000000-0000-4000-8000-000000000101"));

    auto otherScene = make_scene_document("00000000-0000-4000-8000-000000000102", assertContext);
    auto duplicateLocator = take_value(cue::RelativePath::parse("scenes/main.cuescene", assertContext));
    const auto duplicateLocatorResult =
        controller->open_document(std::move(otherScene), std::move(duplicateLocator), true);
    require(!duplicateLocatorResult.has_value());
    require(duplicateLocatorResult.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::editor_core::EditorCoreError::DuplicateLocator));
    require(has_error_context(*duplicateLocatorResult.try_error(), "ConflictingEditorDocumentId=1"));
    require(has_error_context(*duplicateLocatorResult.try_error(), "RequestedSceneLocator"));
    require(has_error_context(*duplicateLocatorResult.try_error(), "scenes/main.cuescene"));
}

/// @brief Dirty が Revision 差だけから一貫して決まることを検証する
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

/// @brief Selection が Stable ObjectId だけを順序付き集合として保持することを検証する
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

/// @brief Scene 編集 Command の Stable ID、完全 Rollback、Subtree 操作を検証する
void test_scene_commands() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    cue::schema::SchemaRegistryIdentitySource registryIdentitySource;
    auto registry = make_component_registry(registryIdentitySource, assertContext);
    auto valueRegistry = make_component_value_registry(*registry, assertContext);
    auto controller = cue::editor_core::EditorController::create(make_project_descriptor(assertContext), assertContext);

    const auto opaqueId = make_component_id("00000000-0000-4000-8000-000000000523", assertContext);
    const auto opaqueCopyId = make_component_id("00000000-0000-4000-8000-000000000524", assertContext);
    auto futureVersion = take_value(cue::schema::SchemaVersion::create(2U, assertContext));
    auto opaqueData = take_value(cue::scene::OpaqueComponentData::create(
        opaqueId, make_component_type_id(assertContext), futureVersion, "{\"future\":true}", *registry, assertContext));
    const auto opaqueDuplicate =
        cue::scene::SceneComponent::opaque(std::move(opaqueData)).duplicate_with_identity(opaqueCopyId, assertContext);
    require(!opaqueDuplicate.has_value());
    require(opaqueDuplicate.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::scene::SceneError::UnsupportedComponentOperation));

    auto scene = make_scene_document("00000000-0000-4000-8000-000000000501", assertContext);
    const auto sceneAssetId = scene.scene_asset_id();
    const auto rootId = make_object_id("00000000-0000-4000-8000-000000000511", assertContext);
    const auto childId = make_object_id("00000000-0000-4000-8000-000000000512", assertContext);
    const auto grandchildId = make_object_id("00000000-0000-4000-8000-000000000513", assertContext);
    const auto componentId = make_component_id("00000000-0000-4000-8000-000000000521", assertContext);
    const auto secondComponentId = make_component_id("00000000-0000-4000-8000-000000000525", assertContext);
    require(scene.add_object(rootId, "Root", true, std::nullopt, cue::math::Transform{}).has_value());
    require(scene.add_object(childId, "Child", true, rootId, cue::math::Transform{}).has_value());
    require(
        scene.add_component(childId, make_health_component(componentId, 100, *registry, valueRegistry, assertContext))
            .has_value());
    require(scene
                .add_component(childId,
                               make_health_component(secondComponentId, 50, *registry, valueRegistry, assertContext))
                .has_value());

    auto checkpoint = scene.create_checkpoint();
    require(scene.remove_component(childId, componentId).has_value());
    require(scene.restore_checkpoint(std::move(checkpoint)).has_value());
    require(component_health(*scene.find_object(childId), componentId) == 100);

    auto locator = take_value(cue::RelativePath::parse("Scenes/Commands.cuescene", assertContext));
    const auto documentId = take_value(controller->open_document(std::move(scene), std::move(locator), true));

    auto state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::AddObjectCommand{grandchildId, "Grandchild", true, childId, cue::math::Transform{}}}));
    require(state.value() == 2U);
    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::RenameObjectCommand{rootId, "Renamed Root"}}));
    require(state.value() == 3U);
    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::ReparentObjectCommand{grandchildId, std::nullopt}}));
    require(state.value() == 4U);
    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::ReparentObjectCommand{grandchildId, childId}}));
    require(state.value() == 5U);

    const auto cycle = controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::ReparentObjectCommand{rootId, childId}});
    require(!cycle.has_value());
    require(has_error_context(*cycle.try_error(), "EditorDocumentId=1"));
    require(has_error_context(*cycle.try_error(), "ObjectId=00000000-0000-4000-8000-000000000511"));
    const auto *document = controller->session().find_document(documentId);
    require(document != nullptr);
    require(document->current_state_id().value() == 5U);
    require(document->scene_document().find_object(rootId)->try_parent_id() == nullptr);

    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::EditFieldCommand{childId, componentId, make_health_field_id(assertContext),
                                           cue::scene::FieldValue::signed_integer(250)}}));
    require(state.value() == 6U);
    document = controller->session().find_document(documentId);
    require(component_health(*document->scene_document().find_object(childId), componentId) == 250);

    const auto fieldMismatch = controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::EditFieldCommand{childId, componentId, make_health_field_id(assertContext),
                                           cue::scene::FieldValue::boolean(true)}});
    require(!fieldMismatch.has_value());
    document = controller->session().find_document(documentId);
    require(document->current_state_id().value() == 6U);
    require(component_health(*document->scene_document().find_object(childId), componentId) == 250);

    auto movedAssetReference = take_value(cue::scene::AssetReferenceValue::create("asset://moved", assertContext));
    const auto consumedAssetValue = cue::scene::FieldValue::asset_reference(std::move(movedAssetReference));
    (void)consumedAssetValue;
    auto invalidAssetValue = cue::scene::FieldValue::asset_reference(std::move(movedAssetReference));
    require(!cue::scene::is_valid_field_value(invalidAssetValue));
    const auto invalidAsset = controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::EditFieldCommand{childId, componentId, make_asset_field_id(assertContext),
                                           std::move(invalidAssetValue)}});
    require(!invalidAsset.has_value());
    require(invalidAsset.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::scene::SceneError::FieldTypeMismatch));
    document = controller->session().find_document(documentId);
    require(document->current_state_id().value() == 6U);
    require(component_asset_token(*document->scene_document().find_object(childId), componentId) == "asset://default");

    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::EditTransformCommand{childId, cue::math::Transform{}}}));
    require(state.value() == 6U);

    const auto rootComponentId = make_component_id("00000000-0000-4000-8000-000000000522", assertContext);
    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::AddComponentCommand{
            rootId, make_health_component(rootComponentId, 10, *registry, valueRegistry, assertContext)}}));
    require(state.value() == 7U);
    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::RemoveComponentCommand{rootId, rootComponentId}}));
    require(state.value() == 8U);

    const auto repeatedChildCopyId = make_object_id("00000000-0000-4000-8000-000000000535", assertContext);
    const auto repeatedGrandchildCopyId = make_object_id("00000000-0000-4000-8000-000000000536", assertContext);
    const auto repeatedComponentCopyId = make_component_id("00000000-0000-4000-8000-000000000537", assertContext);
    std::vector<cue::editor_core::DuplicateObjectTarget> repeatedTargets;
    repeatedTargets.push_back(
        {childId, repeatedChildCopyId, "Child Copy", {repeatedComponentCopyId, repeatedComponentCopyId}});
    repeatedTargets.push_back({grandchildId, repeatedGrandchildCopyId, "Grandchild Copy", {}});
    const auto repeatedDuplicate = controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::DuplicateObjectCommand{childId, std::move(repeatedTargets)}});
    require(!repeatedDuplicate.has_value());
    document = controller->session().find_document(documentId);
    require(document->current_state_id().value() == 8U);
    require(document->scene_document().find_object(repeatedChildCopyId) == nullptr);

    const auto failedChildCopyId = make_object_id("00000000-0000-4000-8000-000000000531", assertContext);
    const auto failedGrandchildCopyId = make_object_id("00000000-0000-4000-8000-000000000532", assertContext);
    const auto failedComponentCopyId = make_component_id("00000000-0000-4000-8000-000000000533", assertContext);
    const auto failedSecondComponentCopyId = make_component_id("00000000-0000-4000-8000-000000000534", assertContext);
    std::vector<cue::editor_core::DuplicateObjectTarget> failedTargets;
    failedTargets.push_back(
        {childId, failedChildCopyId, "Child Copy", {failedComponentCopyId, failedSecondComponentCopyId}});
    failedTargets.push_back({grandchildId, failedGrandchildCopyId, "", {}});
    const auto failedDuplicate = controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::DuplicateObjectCommand{childId, std::move(failedTargets)}});
    require(!failedDuplicate.has_value());
    document = controller->session().find_document(documentId);
    require(document->current_state_id().value() == 8U);
    require(document->scene_document().find_object(failedChildCopyId) == nullptr);
    require(document->scene_document().find_object(failedGrandchildCopyId) == nullptr);
    require(component_health(*document->scene_document().find_object(childId), componentId) == 250);

    const auto childCopyId = make_object_id("00000000-0000-4000-8000-000000000541", assertContext);
    const auto grandchildCopyId = make_object_id("00000000-0000-4000-8000-000000000542", assertContext);
    const auto componentCopyId = make_component_id("00000000-0000-4000-8000-000000000543", assertContext);
    const auto secondComponentCopyId = make_component_id("00000000-0000-4000-8000-000000000544", assertContext);
    std::vector<cue::editor_core::DuplicateObjectTarget> targets;
    targets.push_back({childId, childCopyId, "Child Copy", {componentCopyId, secondComponentCopyId}});
    targets.push_back({grandchildId, grandchildCopyId, "Grandchild Copy", {}});
    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::DuplicateObjectCommand{childId, std::move(targets)}}));
    require(state.value() == 9U);
    document = controller->session().find_document(documentId);
    const auto *childCopy = document->scene_document().find_object(childCopyId);
    const auto *grandchildCopy = document->scene_document().find_object(grandchildCopyId);
    require(childCopy != nullptr);
    require(grandchildCopy != nullptr);
    require(component_health(*childCopy, componentCopyId) == 250);
    require(component_health(*childCopy, secondComponentCopyId) == 50);
    require(grandchildCopy->try_parent_id() != nullptr && *grandchildCopy->try_parent_id() == childCopyId);

    auto otherSceneId =
        take_value(cue::scene::SceneAssetId::parse("00000000-0000-4000-8000-000000000599", assertContext));
    const auto crossDocument = controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, otherSceneId, cue::editor_core::RenameObjectCommand{rootId, "Wrong Scene"}});
    require(!crossDocument.has_value());
    require(crossDocument.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::editor_core::EditorCoreError::SceneMismatch));
    document = controller->session().find_document(documentId);
    require(document->scene_document().find_object(rootId)->name() == "Renamed Root");
    require(document->current_state_id().value() == 9U);

    const std::array selection{childId, grandchildId};
    require(controller->set_selection(documentId, selection, &childId).has_value());
    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::DeleteObjectCommand{childId}}));
    require(state.value() == 10U);
    document = controller->session().find_document(documentId);
    require(document->scene_document().find_object(childId) == nullptr);
    require(document->scene_document().find_object(grandchildId) == nullptr);
    require(document->selection().empty());
    require(document->scene_document().find_object(childCopyId) != nullptr);
}

/// @brief Transaction単位のUndo／Redo、分岐破棄、連続編集復元を検証する
void test_transaction_history() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    cue::schema::SchemaRegistryIdentitySource registryIdentitySource;
    auto registry = make_component_registry(registryIdentitySource, assertContext);
    auto valueRegistry = make_component_value_registry(*registry, assertContext);
    auto controller = cue::editor_core::EditorController::create(make_project_descriptor(assertContext), assertContext);

    auto scene = make_scene_document("00000000-0000-4000-8000-000000000601", assertContext);
    const auto sceneAssetId = scene.scene_asset_id();
    const auto rootId = make_object_id("00000000-0000-4000-8000-000000000611", assertContext);
    const auto childId = make_object_id("00000000-0000-4000-8000-000000000612", assertContext);
    const auto grandchildId = make_object_id("00000000-0000-4000-8000-000000000613", assertContext);
    const auto componentId = make_component_id("00000000-0000-4000-8000-000000000621", assertContext);
    require(scene.add_object(rootId, "Root", true, std::nullopt, cue::math::Transform{}).has_value());
    auto locator = take_value(cue::RelativePath::parse("Scenes/History.cuescene", assertContext));
    const auto documentId = take_value(controller->open_document(std::move(scene), std::move(locator), true));

    cue::editor_core::EditorTransaction failedTransaction{"Fail Together", {}};
    failedTransaction.commands.push_back(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::RenameObjectCommand{rootId, "Partial Name"}});
    failedTransaction.commands.push_back(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::AddObjectCommand{rootId, "Duplicate", true, std::nullopt, cue::math::Transform{}}});
    require(!controller->execute_transaction(std::move(failedTransaction)).has_value());
    const auto *document = controller->session().find_document(documentId);
    require(document != nullptr && document->current_state_id().value() == 1U);
    require(document->scene_document().find_object(rootId)->name() == "Root");
    require(document->history_entry_count() == 0U);

    cue::editor_core::EditorTransaction addTransaction{"Add Character", {}};
    addTransaction.commands.push_back(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::AddObjectCommand{childId, "Child", true, rootId, cue::math::Transform{}}});
    addTransaction.commands.push_back(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::AddComponentCommand{
            childId, make_health_component(componentId, 77, *registry, valueRegistry, assertContext)}});
    addTransaction.commands.push_back(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId,
        cue::editor_core::AddObjectCommand{grandchildId, "Grandchild", false, childId, cue::math::Transform{}}});
    auto state = take_value(controller->execute_transaction(std::move(addTransaction)));
    require(state.value() == 2U);
    document = controller->session().find_document(documentId);
    require(document != nullptr && document->history_entry_count() == 1U && document->history_byte_size() > 0U);
    require(document->history_byte_size() <= cue::editor_core::EditorDocument::maximum_history_bytes());
    require(document->undo_label() == "Add Character");
    require(component_health(*document->scene_document().find_object(childId), componentId) == 77);
    require(document->scene_document().find_object(grandchildId)->try_parent_id() != nullptr);

    state = take_value(controller->undo(documentId));
    require(state.value() == 1U);
    document = controller->session().find_document(documentId);
    require(document->scene_document().find_object(childId) == nullptr);
    require(!document->is_dirty() && document->can_redo());
    require(document->redo_label() == "Add Character");
    state = take_value(controller->redo(documentId));
    require(state.value() == 2U);
    document = controller->session().find_document(documentId);
    require(component_health(*document->scene_document().find_object(childId), componentId) == 77);
    require(document->is_dirty());

    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::DeleteObjectCommand{childId}}));
    require(state.value() == 3U);
    document = controller->session().find_document(documentId);
    require(document->scene_document().find_object(childId) == nullptr);
    state = take_value(controller->undo(documentId));
    require(state.value() == 2U);
    document = controller->session().find_document(documentId);
    require(component_health(*document->scene_document().find_object(childId), componentId) == 77);
    const auto *restoredGrandchild = document->scene_document().find_object(grandchildId);
    require(restoredGrandchild != nullptr && restoredGrandchild->try_parent_id() != nullptr &&
            *restoredGrandchild->try_parent_id() == childId);
    require(take_value(controller->redo(documentId)).value() == 3U);
    require(take_value(controller->undo(documentId)).value() == 2U);

    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::RemoveComponentCommand{childId, componentId}}));
    require(state.value() == 4U);
    document = controller->session().find_document(documentId);
    require(document->scene_document().find_object(childId)->components().empty());
    require(take_value(controller->undo(documentId)).value() == 2U);
    document = controller->session().find_document(documentId);
    require(component_health(*document->scene_document().find_object(childId), componentId) == 77);

    state = take_value(controller->execute_command(cue::editor_core::SceneCommandRequest{
        documentId, sceneAssetId, cue::editor_core::RenameObjectCommand{rootId, "Branch Root"}}));
    require(state.value() == 5U);
    document = controller->session().find_document(documentId);
    require(!document->can_redo());

    for (std::size_t index = 0U; index < 100U; ++index)
    {
        const std::string_view name = index % 2U == 0U ? "Loop A" : "Loop B";
        require(controller->execute_command(cue::editor_core::SceneCommandRequest{
                    documentId, sceneAssetId, cue::editor_core::RenameObjectCommand{rootId, std::string(name)}})
                    .has_value());
    }
    document = controller->session().find_document(documentId);
    require(document->current_state_id().value() == 105U);
    for (std::size_t index = 0U; index < 100U; ++index)
    {
        require(controller->undo(documentId).has_value());
    }
    document = controller->session().find_document(documentId);
    require(document->current_state_id().value() == 5U);
    require(document->scene_document().find_object(rootId)->name() == "Branch Root");
    for (std::size_t index = 0U; index < 100U; ++index)
    {
        require(controller->redo(documentId).has_value());
    }
    document = controller->session().find_document(documentId);
    require(document->current_state_id().value() == 105U);
    require(document->scene_document().find_object(rootId)->name() == "Loop B");
    require(document->scene_document().validate().has_value());

    for (std::size_t index = 0U; index < 160U; ++index)
    {
        const std::string_view name = index % 2U == 0U ? "Limit A" : "Limit B";
        require(controller->execute_command(cue::editor_core::SceneCommandRequest{
                    documentId, sceneAssetId, cue::editor_core::RenameObjectCommand{rootId, std::string(name)}})
                    .has_value());
    }
    document = controller->session().find_document(documentId);
    require(document->history_entry_count() == cue::editor_core::EditorDocument::maximum_history_entries());
    require(document->history_byte_size() <= cue::editor_core::EditorDocument::maximum_history_bytes());

    require(controller->undo(documentId).has_value());
    document = controller->session().find_document(documentId);
    require(document->can_undo() && document->can_redo());
    require(controller->record_persistent_change(documentId).has_value());
    document = controller->session().find_document(documentId);
    require(!document->can_undo() && !document->can_redo());
    require(document->history_entry_count() == 0U && document->history_byte_size() == 0U);

    require(controller->execute_command(cue::editor_core::SceneCommandRequest{
                documentId, sceneAssetId, cue::editor_core::RenameObjectCommand{rootId, "History Restart"}})
                .has_value());
    document = controller->session().find_document(documentId);
    require(document->can_undo() && !document->can_redo());
    require(controller->record_persistent_change(documentId).has_value());
    document = controller->session().find_document(documentId);
    require(!document->can_undo() && !document->can_redo());
}

/// @brief 外部変更と Close 判断の状態遷移を検証する
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

    auto failedSaveScene = make_scene_document("00000000-0000-4000-8000-000000000406", assertContext);
    auto failedSaveLocator = take_value(cue::RelativePath::parse("Scenes/FailedSave.cuescene", assertContext));
    const auto failedSaveId =
        take_value(controller->open_document(std::move(failedSaveScene), std::move(failedSaveLocator), false));
    require(take_value(controller->request_close(failedSaveId)) ==
            cue::editor_core::DocumentCloseState::AwaitingDecision);
    require(take_value(controller->respond_to_close(failedSaveId, cue::editor_core::CloseDecision::Save)) ==
            cue::editor_core::DocumentCloseState::SaveRequested);
    require(take_value(controller->report_save_failure(failedSaveId)) ==
            cue::editor_core::DocumentCloseState::AwaitingDecision);
    require(take_value(controller->respond_to_close(failedSaveId, cue::editor_core::CloseDecision::Cancel)) ==
            cue::editor_core::DocumentCloseState::Open);

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
    const auto conflictingSave = controller->respond_to_close(changedId, cue::editor_core::CloseDecision::Save);
    require(!conflictingSave.has_value());
    require(conflictingSave.try_error()->code().value() ==
            static_cast<std::int64_t>(cue::editor_core::EditorCoreError::InvalidCloseTransition));
    const auto *changedDocument = controller->session().find_document(changedId);
    require(changedDocument != nullptr);
    require(changedDocument->close_state() == cue::editor_core::DocumentCloseState::AwaitingDecision);
}
} // namespace

int main()
{
    test_workspace_and_open();
    test_revision_and_dirty();
    test_selection_reconciliation();
    test_scene_commands();
    test_transaction_history();
    test_external_change_and_close();
    return 0;
}
