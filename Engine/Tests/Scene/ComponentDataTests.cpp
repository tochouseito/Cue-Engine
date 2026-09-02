#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Scene/ComponentData.h>
#include <Cue/Scene/Error.h>
#include <Cue/Scene/SceneDocument.h>
#include <Cue/Schema/Descriptor.h>

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
    /// @brief Test中の通常FatalをProcess失敗へ変換する
    [[noreturn]] void terminate() noexcept override
    {
        std::abort();
    }

    /// @brief Test中のEmergency FatalをProcess失敗へ変換する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

class SequentialIdentitySource final : public cue::scene::SceneIdentitySource
{
  public:
    /// @brief Testごとに異なる有効なUUID Version 4を返す
    [[nodiscard]] cue::scene::IdentityBytes next_identity() noexcept override
    {
        cue::scene::IdentityBytes bytes{};
        bytes[6] = 0x40U;
        bytes[8] = 0x80U;
        bytes[14] = static_cast<std::uint8_t>((m_next >> 8U) & 0xFFU);
        bytes[15] = static_cast<std::uint8_t>(m_next & 0xFFU);
        ++m_next;
        return bytes;
    }

  private:
    std::uint16_t m_next = 1U;
};

/// @brief 条件が偽ならTest Processを失敗終了する
void require(bool a_condition) noexcept
{
    if (!a_condition)
    {
        std::abort();
    }
}

/// @brief 成功Resultから所有Valueを取り出す
template <typename T> T take_value(cue::Result<T> &&a_result) noexcept
{
    require(a_result.has_value());
    return std::move(*a_result.try_value());
}

/// @brief Test用Stable TypeIdを生成する
[[nodiscard]] cue::schema::TypeId make_type_id(
    const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::schema::TypeId::parse(
        "10000000-0000-4000-8000-000000000001", a_assertContext));
}

/// @brief Test用Stable FieldIdを生成する
[[nodiscard]] cue::schema::FieldId make_field_id(
    std::uint32_t a_value,
    const cue::AssertContext &a_assertContext) noexcept
{
    return take_value(cue::schema::FieldId::create(a_value, a_assertContext));
}

/// @brief Test用Schema Versionを生成する
[[nodiscard]] cue::schema::SchemaVersion make_version(
    const cue::AssertContext &a_assertContext,
    std::uint32_t a_value = 1U) noexcept
{
    return take_value(cue::schema::SchemaVersion::create(a_value, a_assertContext));
}

/// @brief Field 1と2を持つTest用Immutable Schema Registryを構築する
[[nodiscard]] std::unique_ptr<cue::schema::SchemaRegistry> make_registry(
    cue::schema::SchemaRegistryIdentitySource &a_identitySource,
    const cue::AssertContext &a_assertContext) noexcept
{
    std::vector<cue::schema::FieldDescriptor> fields;
    fields.push_back(take_value(cue::schema::create_field_descriptor(
        make_field_id(1U, a_assertContext), "health", a_assertContext)));
    fields.push_back(take_value(cue::schema::create_field_descriptor(
        make_field_id(2U, a_assertContext), "label", a_assertContext)));
    std::vector<cue::schema::FieldId> reserved;
    auto descriptor = take_value(cue::schema::create_type_descriptor(
        make_type_id(a_assertContext), "Cue.Test.Component",
        make_version(a_assertContext), std::move(fields), std::move(reserved),
        a_assertContext));
    cue::schema::SchemaRegistryBuilder builder(a_identitySource, a_assertContext);
    require(builder.add_type(std::move(descriptor)).has_value());
    return take_value(builder.seal());
}

/// @brief 既知Schema値、未知Field、Opaque Componentの保持契約を検証する
void test_component_data() noexcept
{
    TestFatalHandler fatalHandler;
    std::vector<std::unique_ptr<cue::LogSink>> sinks;
    cue::Logger logger(fatalHandler, std::move(sinks));
    cue::AssertContext assertContext(logger, fatalHandler);
    cue::schema::SchemaRegistryIdentitySource registryIdentitySource;
    auto registry = make_registry(registryIdentitySource, assertContext);
    SequentialIdentitySource sceneIdentitySource;

    const auto field1 = make_field_id(1U, assertContext);
    const auto field2 = make_field_id(2U, assertContext);
    const auto unknownField = make_field_id(99U, assertContext);
    std::vector<cue::scene::FieldKindBinding> invalidBindings{
        {field1, static_cast<cue::scene::FieldValueKind>(255U)},
        {field2, cue::scene::FieldValueKind::String}};
    const auto invalidValueSchema = cue::scene::create_component_value_schema(
        make_type_id(assertContext), make_version(assertContext),
        std::move(invalidBindings), *registry, assertContext);
    require(!invalidValueSchema.has_value());
    std::vector<cue::scene::FieldKindBinding> movedSchemaBindings{
        {field1, cue::scene::FieldValueKind::SignedInteger},
        {field2, cue::scene::FieldValueKind::String}};
    auto movedSchema = take_value(cue::scene::create_component_value_schema(
        make_type_id(assertContext), make_version(assertContext),
        std::move(movedSchemaBindings), *registry, assertContext));
    std::vector<cue::scene::ComponentValueSchema> consumedSchemas;
    consumedSchemas.push_back(std::move(movedSchema));
    (void)consumedSchemas;
    std::vector<cue::scene::ComponentValueSchema> reusedSchemas;
    reusedSchemas.push_back(std::move(movedSchema));
    const auto movedSchemaRegistry =
        cue::scene::ComponentValueSchemaRegistry::create(
            std::move(reusedSchemas), *registry, assertContext);
    require(!movedSchemaRegistry.has_value());
    std::vector<cue::scene::FieldKindBinding> bindings{
        {field1, cue::scene::FieldValueKind::SignedInteger},
        {field2, cue::scene::FieldValueKind::String}};
    std::vector<cue::scene::ComponentValueSchema> valueSchemas;
    valueSchemas.push_back(take_value(cue::scene::create_component_value_schema(
        make_type_id(assertContext), make_version(assertContext),
        std::move(bindings), *registry, assertContext)));
    auto valueSchemaRegistry = take_value(
        cue::scene::ComponentValueSchemaRegistry::create(
            std::move(valueSchemas), *registry, assertContext));

    auto movedOpaqueField = take_value(cue::scene::OpaqueFieldData::create(
        unknownField, "{\"future\":true}", assertContext));
    std::vector<cue::scene::OpaqueFieldData> consumedOpaqueFields;
    consumedOpaqueFields.push_back(std::move(movedOpaqueField));
    (void)consumedOpaqueFields;
    std::vector<cue::scene::OpaqueFieldData> reusedOpaqueFields;
    reusedOpaqueFields.push_back(std::move(movedOpaqueField));
    std::vector<cue::scene::KnownFieldData> noMovedKnownFields;
    auto movedFieldComponentId = take_value(
        cue::scene::ComponentInstanceId::generate(sceneIdentitySource,
                                                   assertContext));
    const auto movedOpaqueComponent = cue::scene::create_known_component(
        std::move(movedFieldComponentId), make_type_id(assertContext),
        make_version(assertContext), std::move(noMovedKnownFields),
        std::move(reusedOpaqueFields), *registry, valueSchemaRegistry,
        assertContext);
    require(!movedOpaqueComponent.has_value());

    std::vector<cue::scene::KnownFieldData> knownFields;
    knownFields.push_back(take_value(cue::scene::create_known_field(
        field2, take_value(cue::scene::FieldValue::string("Player", assertContext)),
        cue::scene::FieldValueKind::String, assertContext)));
    knownFields.push_back(take_value(cue::scene::create_known_field(
        field1, cue::scene::FieldValue::signed_integer(100),
        cue::scene::FieldValueKind::SignedInteger, assertContext)));
    std::vector<cue::scene::OpaqueFieldData> unknownFields;
    unknownFields.push_back(take_value(cue::scene::OpaqueFieldData::create(
        unknownField, "{\"future\":[1,2,3]}", assertContext)));
    auto componentId = take_value(cue::scene::ComponentInstanceId::generate(
        sceneIdentitySource, assertContext));
    const auto preservedComponentId = componentId;
    auto knownComponent = take_value(cue::scene::create_known_component(
        std::move(componentId), make_type_id(assertContext),
        make_version(assertContext), std::move(knownFields),
        std::move(unknownFields), *registry, valueSchemaRegistry, assertContext));
    require(knownComponent.known_fields().size() == 2U);
    require(knownComponent.known_fields()[0].id() == field1);
    require(knownComponent.unknown_fields()[0].raw_json() ==
            "{\"future\":[1,2,3]}");

    const auto mismatch = cue::scene::create_known_field(
        field1, cue::scene::FieldValue::boolean(true),
        cue::scene::FieldValueKind::SignedInteger, assertContext);
    require(!mismatch.has_value());
    require(mismatch.try_error()->code().value() == static_cast<std::int64_t>(
               cue::scene::SceneError::FieldTypeMismatch));
    const std::string_view invalidUtf8("\xC3\x28", 2U);
    const auto invalidString = cue::scene::FieldValue::string(invalidUtf8,
                                                              assertContext);
    require(!invalidString.has_value());
    const std::string oversizedWireString(
        cue::scene::k_maximumSceneStringBytes + 1U, 's');
    require(!cue::scene::FieldValue::string(oversizedWireString,
                                            assertContext)
                 .has_value());
    require(!cue::scene::AssetReferenceValue::create(oversizedWireString,
                                                      assertContext)
                 .has_value());
    const std::string longAssetToken(256U, 'a');
    const auto validLongAssetToken = cue::scene::AssetReferenceValue::create(
        longAssetToken, assertContext);
    require(validLongAssetToken.has_value());
    auto movedAssetToken = take_value(cue::scene::AssetReferenceValue::create(
        "asset:test", assertContext));
    const auto consumedAssetValue = cue::scene::FieldValue::asset_reference(
        std::move(movedAssetToken));
    (void)consumedAssetValue;
    const auto movedAssetField = cue::scene::create_known_field(
        field1,
        cue::scene::FieldValue::asset_reference(std::move(movedAssetToken)),
        cue::scene::FieldValueKind::AssetReference, assertContext);
    require(!movedAssetField.has_value());
    const auto invalidOpaqueField = cue::scene::OpaqueFieldData::create(
        unknownField, invalidUtf8, assertContext);
    require(!invalidOpaqueField.has_value());
    const auto malformedOpaqueField = cue::scene::OpaqueFieldData::create(
        unknownField, "{\"broken\":", assertContext);
    require(!malformedOpaqueField.has_value());
    const auto duplicateMemberField = cue::scene::OpaqueFieldData::create(
        unknownField, "{\"same\":1,\"same\":2}", assertContext);
    require(!duplicateMemberField.has_value());
    const std::string oversizedOpaqueString =
        std::string("\"") +
        std::string(cue::scene::k_maximumSceneStringBytes + 1U, 's') + "\"";
    require(!cue::scene::OpaqueFieldData::create(
                 unknownField, oversizedOpaqueString, assertContext)
                 .has_value());
    const std::string overNestedOpaqueValue =
        std::string(58U, '[') + "0" + std::string(58U, ']');
    require(!cue::scene::OpaqueFieldData::create(
                 unknownField, overNestedOpaqueValue, assertContext)
                 .has_value());
    const std::string overNestedEmptyOpaqueValue =
        std::string(58U, '[') + std::string(58U, ']');
    require(!cue::scene::OpaqueFieldData::create(
                 unknownField, overNestedEmptyOpaqueValue, assertContext)
                 .has_value());
    std::string oversizedOpaqueDocument("[");
    const std::string maximumOpaqueString(
        cue::scene::k_maximumSceneStringBytes, 's');
    for (std::size_t index = 0U; index < 65U; ++index)
    {
        if (index > 0U)
        {
            oversizedOpaqueDocument.push_back(',');
        }
        oversizedOpaqueDocument.push_back('"');
        oversizedOpaqueDocument.append(maximumOpaqueString);
        oversizedOpaqueDocument.push_back('"');
    }
    oversizedOpaqueDocument.push_back(']');
    require(oversizedOpaqueDocument.size() >
            cue::scene::k_maximumSceneBytes);
    require(!cue::scene::OpaqueFieldData::create(
                 unknownField, oversizedOpaqueDocument, assertContext)
                 .has_value());

    std::vector<cue::scene::KnownFieldData> repeatedFields;
    repeatedFields.push_back(take_value(cue::scene::create_known_field(
        field1, cue::scene::FieldValue::signed_integer(1),
        cue::scene::FieldValueKind::SignedInteger, assertContext)));
    repeatedFields.push_back(take_value(cue::scene::create_known_field(
        field1, cue::scene::FieldValue::signed_integer(2),
        cue::scene::FieldValueKind::SignedInteger, assertContext)));
    std::vector<cue::scene::OpaqueFieldData> noRepeatedUnknownFields;
    auto repeatedComponentId = take_value(
        cue::scene::ComponentInstanceId::generate(sceneIdentitySource,
                                                   assertContext));
    const auto repeated = cue::scene::create_known_component(
        std::move(repeatedComponentId), make_type_id(assertContext),
        make_version(assertContext), std::move(repeatedFields),
        std::move(noRepeatedUnknownFields), *registry, valueSchemaRegistry,
        assertContext);
    require(!repeated.has_value());
    require(repeated.try_error()->code().value() == static_cast<std::int64_t>(
               cue::scene::SceneError::DuplicateFieldId));

    auto newerRegistry = make_registry(registryIdentitySource, assertContext);
    std::vector<cue::scene::KnownFieldData> staleFields;
    staleFields.push_back(take_value(cue::scene::create_known_field(
        field1, cue::scene::FieldValue::signed_integer(5),
        cue::scene::FieldValueKind::SignedInteger, assertContext)));
    std::vector<cue::scene::OpaqueFieldData> noStaleUnknownFields;
    auto staleComponentId = take_value(
        cue::scene::ComponentInstanceId::generate(sceneIdentitySource,
                                                   assertContext));
    const auto staleSchema = cue::scene::create_known_component(
        std::move(staleComponentId), make_type_id(assertContext),
        make_version(assertContext), std::move(staleFields),
        std::move(noStaleUnknownFields), *newerRegistry, valueSchemaRegistry,
        assertContext);
    require(!staleSchema.has_value());

    auto sceneId = take_value(cue::scene::SceneAssetId::generate(
        sceneIdentitySource, assertContext));
    auto objectId = take_value(cue::scene::ObjectId::generate(
        sceneIdentitySource, assertContext));
    auto document = cue::scene::SceneDocument::create(std::move(sceneId),
                                                       assertContext);
    require(document.add_object(objectId, "Object", true, std::nullopt,
                                cue::math::Transform{})
                .has_value());
    require(document.add_component(
                        objectId,
                        cue::scene::SceneComponent::known(
                            std::move(knownComponent)))
                .has_value());
    const auto movedKnownResult = document.add_component(
        objectId, cue::scene::SceneComponent::known(std::move(knownComponent)));
    require(!movedKnownResult.has_value());
    require(movedKnownResult.try_error()->code().value() ==
            static_cast<std::int64_t>(
                cue::scene::SceneError::InvalidComponentData));

    auto opaqueId = take_value(cue::scene::ComponentInstanceId::generate(
        sceneIdentitySource, assertContext));
    auto incorrectlyOpaqueId = take_value(
        cue::scene::ComponentInstanceId::generate(sceneIdentitySource,
                                                   assertContext));
    const auto incorrectlyOpaque = cue::scene::OpaqueComponentData::create(
        std::move(incorrectlyOpaqueId), make_type_id(assertContext),
        make_version(assertContext), "{\"known\":true}", *registry,
        assertContext);
    require(!incorrectlyOpaque.has_value());
    auto nonObjectOpaqueId = take_value(
        cue::scene::ComponentInstanceId::generate(sceneIdentitySource,
                                                   assertContext));
    const auto nonObjectOpaque = cue::scene::OpaqueComponentData::create(
        std::move(nonObjectOpaqueId), make_type_id(assertContext),
        make_version(assertContext, 2U), "true", *registry, assertContext);
    require(!nonObjectOpaque.has_value());
    auto metadataOpaqueId = take_value(
        cue::scene::ComponentInstanceId::generate(sceneIdentitySource,
                                                   assertContext));
    const auto metadataOpaque = cue::scene::OpaqueComponentData::create(
        std::move(metadataOpaqueId), make_type_id(assertContext),
        make_version(assertContext, 2U), "{\"typeId\":\"conflict\"}",
        *registry, assertContext);
    require(!metadataOpaque.has_value());
    auto opaque = take_value(cue::scene::OpaqueComponentData::create(
        std::move(opaqueId), make_type_id(assertContext),
        make_version(assertContext, 2U),
        "{\"future\":true}",
        *registry, assertContext));
    require(document.add_component(
                        objectId,
                        cue::scene::SceneComponent::opaque(std::move(opaque)))
                .has_value());
    const auto movedOpaqueResult = document.add_component(
        objectId, cue::scene::SceneComponent::opaque(std::move(opaque)));
    require(!movedOpaqueResult.has_value());
    require(movedOpaqueResult.try_error()->code().value() ==
            static_cast<std::int64_t>(
                cue::scene::SceneError::InvalidComponentData));
    require(document.find_object(objectId)->components().size() == 2U);
    require(document.validate().has_value());

    std::vector<cue::scene::KnownFieldData> duplicateFields;
    duplicateFields.push_back(take_value(cue::scene::create_known_field(
        field1, cue::scene::FieldValue::signed_integer(1),
        cue::scene::FieldValueKind::SignedInteger, assertContext)));
    std::vector<cue::scene::OpaqueFieldData> noUnknownFields;
    auto duplicateComponent = take_value(cue::scene::create_known_component(
        preservedComponentId, make_type_id(assertContext),
        make_version(assertContext), std::move(duplicateFields),
        std::move(noUnknownFields), *registry, valueSchemaRegistry,
        assertContext));
    const auto duplicateResult = document.add_component(
        objectId,
        cue::scene::SceneComponent::known(std::move(duplicateComponent)));
    require(!duplicateResult.has_value());
    require(document.find_object(objectId)->components().size() == 2U);
}

} // namespace

/// @brief Cue.SceneのSchema駆動Component Data Testを実行する
int main()
{
    test_component_data();
    return 0;
}
