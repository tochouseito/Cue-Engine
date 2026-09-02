#include <Cue/Scene/ComponentData.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Scene/Error.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
/// @brief Scene Component Data Allocation失敗をEmergency終了へ変換する
[[noreturn]] void terminate_scene_allocation(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate(
        "Cue.Scene component data allocation failed");
}

/// @brief UTF-8継続Byteか判定する
[[nodiscard]] bool is_continuation(std::uint8_t a_value) noexcept
{
    return (a_value & 0xC0U) == 0x80U;
}

/// @brief TextがUnicode Scalar列として有効なUTF-8か検証する
[[nodiscard]] bool is_valid_utf8(std::string_view a_value) noexcept
{
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(a_value.data());
    for (std::size_t index = 0U; index < a_value.size();)
    {
        const auto first = bytes[index];
        if (first <= 0x7FU)
        {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU)
        {
            if (index + 1U >= a_value.size() ||
                !is_continuation(bytes[index + 1U]))
            {
                return false;
            }
            index += 2U;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU)
        {
            if (index + 2U >= a_value.size() ||
                !is_continuation(bytes[index + 1U]) ||
                !is_continuation(bytes[index + 2U]) ||
                (first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
                (first == 0xEDU && bytes[index + 1U] >= 0xA0U))
            {
                return false;
            }
            index += 3U;
            continue;
        }
        if (first >= 0xF0U && first <= 0xF4U)
        {
            if (index + 3U >= a_value.size() ||
                !is_continuation(bytes[index + 1U]) ||
                !is_continuation(bytes[index + 2U]) ||
                !is_continuation(bytes[index + 3U]) ||
                (first == 0xF0U && bytes[index + 1U] < 0x90U) ||
                (first == 0xF4U && bytes[index + 1U] >= 0x90U))
            {
                return false;
            }
            index += 4U;
            continue;
        }
        return false;
    }
    return true;
}

/// @brief Field Kind BindingをStable FieldId順で検索する
[[nodiscard]] const cue::scene::FieldKindBinding *find_binding(
    std::span<const cue::scene::FieldKindBinding> a_bindings,
    cue::schema::FieldId a_id) noexcept
{
    const auto found = std::lower_bound(
        a_bindings.begin(), a_bindings.end(), a_id,
        /// @brief BindingのFieldIdを検索値と比較する
        [](const cue::scene::FieldKindBinding &a_binding,
           cue::schema::FieldId a_value) noexcept
        {
            return a_binding.id < a_value;
        });
    return found != a_bindings.end() && found->id == a_id ? &*found : nullptr;
}

/// @brief Schema DescriptorにStable FieldIdが存在するか判定する
[[nodiscard]] bool descriptor_has_field(
    const cue::schema::TypeDescriptor &a_descriptor,
    cue::schema::FieldId a_id) noexcept
{
    return std::any_of(
        a_descriptor.fields().begin(), a_descriptor.fields().end(),
        /// @brief Descriptor FieldのIdentityが検索値と一致するか判定する
        [a_id](const cue::schema::FieldDescriptor &a_field) noexcept
        {
            return a_field.id() == a_id;
        });
}
} // namespace

namespace cue::scene
{
AssetReferenceValue::AssetReferenceValue(std::string a_token) noexcept
    : m_token(std::move(a_token))
{
}

Result<AssetReferenceValue> AssetReferenceValue::create(
    std::string_view a_token,
    const AssertContext &a_assertContext) noexcept
{
    if (a_token.empty() || a_token.size() > 255U || !is_valid_utf8(a_token))
    {
        return Result<AssetReferenceValue>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Asset reference token must contain 1 to 255 bytes"));
    }
    try
    {
        return Result<AssetReferenceValue>::success(
            AssetReferenceValue(std::string(a_token)));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

std::string_view AssetReferenceValue::token() const noexcept
{
    return m_token;
}

FieldValue::FieldValue(Storage a_storage) noexcept
    : m_storage(std::move(a_storage))
{
}

FieldValue FieldValue::boolean(bool a_value) noexcept
{
    return FieldValue(Storage(std::in_place_type<bool>, a_value));
}

FieldValue FieldValue::signed_integer(std::int64_t a_value) noexcept
{
    return FieldValue(Storage(std::in_place_type<std::int64_t>, a_value));
}

FieldValue FieldValue::unsigned_integer(std::uint64_t a_value) noexcept
{
    return FieldValue(Storage(std::in_place_type<std::uint64_t>, a_value));
}

Result<FieldValue> FieldValue::floating_point(
    double a_value, const AssertContext &a_assertContext) noexcept
{
    if (!std::isfinite(a_value))
    {
        return Result<FieldValue>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Scene floating point field must be finite"));
    }
    return Result<FieldValue>::success(
        FieldValue(Storage(std::in_place_type<double>, a_value)));
}

Result<FieldValue> FieldValue::string(
    std::string_view a_value, const AssertContext &a_assertContext) noexcept
{
    if (!is_valid_utf8(a_value))
    {
        return Result<FieldValue>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Scene string field must contain valid UTF-8"));
    }
    try
    {
        return Result<FieldValue>::success(FieldValue(
            Storage(std::in_place_type<std::string>, a_value)));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

FieldValue FieldValue::asset_reference(AssetReferenceValue a_value) noexcept
{
    return FieldValue(Storage(std::in_place_type<AssetReferenceValue>,
                              std::move(a_value)));
}

FieldValueKind FieldValue::kind() const noexcept
{
    return static_cast<FieldValueKind>(m_storage.index());
}

const bool *FieldValue::try_boolean() const noexcept
{
    return std::get_if<bool>(&m_storage);
}

const std::int64_t *FieldValue::try_signed_integer() const noexcept
{
    return std::get_if<std::int64_t>(&m_storage);
}

const std::uint64_t *FieldValue::try_unsigned_integer() const noexcept
{
    return std::get_if<std::uint64_t>(&m_storage);
}

const double *FieldValue::try_floating_point() const noexcept
{
    return std::get_if<double>(&m_storage);
}

const std::string *FieldValue::try_string() const noexcept
{
    return std::get_if<std::string>(&m_storage);
}

const AssetReferenceValue *FieldValue::try_asset_reference() const noexcept
{
    return std::get_if<AssetReferenceValue>(&m_storage);
}

KnownFieldData::KnownFieldData(schema::FieldId a_id,
                               FieldValue a_value) noexcept
    : m_id(a_id), m_value(std::move(a_value))
{
}

schema::FieldId KnownFieldData::id() const noexcept
{
    return m_id;
}

const FieldValue &KnownFieldData::value() const noexcept
{
    return m_value;
}

OpaqueFieldData::OpaqueFieldData(schema::FieldId a_id,
                                 std::string a_rawJson) noexcept
    : m_id(a_id), m_rawJson(std::move(a_rawJson))
{
}

Result<OpaqueFieldData> OpaqueFieldData::create(
    schema::FieldId a_id, std::string_view a_rawJson,
    const AssertContext &a_assertContext) noexcept
{
    if (a_rawJson.empty() || !is_valid_utf8(a_rawJson))
    {
        return Result<OpaqueFieldData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidOpaqueData,
            "Opaque field JSON payload must not be empty"));
    }
    try
    {
        return Result<OpaqueFieldData>::success(
            OpaqueFieldData(a_id, std::string(a_rawJson)));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

schema::FieldId OpaqueFieldData::id() const noexcept
{
    return m_id;
}

std::string_view OpaqueFieldData::raw_json() const noexcept
{
    return m_rawJson;
}

ComponentValueSchema::ComponentValueSchema(
    schema::TypeId a_typeId, schema::SchemaVersion a_version,
    std::vector<FieldKindBinding> a_fieldKinds) noexcept
    : m_typeId(a_typeId), m_version(a_version),
      m_fieldKinds(std::move(a_fieldKinds))
{
}

schema::TypeId ComponentValueSchema::type_id() const noexcept
{
    return m_typeId;
}

schema::SchemaVersion ComponentValueSchema::version() const noexcept
{
    return m_version;
}

std::span<const FieldKindBinding> ComponentValueSchema::field_kinds() const noexcept
{
    return m_fieldKinds;
}

ComponentValueSchemaRegistry::ComponentValueSchemaRegistry(
    std::vector<ComponentValueSchema> a_schemas) noexcept
    : m_schemas(std::move(a_schemas))
{
}

Result<ComponentValueSchemaRegistry> ComponentValueSchemaRegistry::create(
    std::vector<ComponentValueSchema> a_schemas,
    const AssertContext &a_assertContext) noexcept
{
    try
    {
        std::sort(a_schemas.begin(), a_schemas.end(),
                  /// @brief Value SchemaをStable TypeId順へ並べる
                  [](const ComponentValueSchema &a_left,
                     const ComponentValueSchema &a_right) noexcept
                  {
                      return a_left.type_id() < a_right.type_id();
                  });
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
    for (std::size_t index = 1U; index < a_schemas.size(); ++index)
    {
        if (a_schemas[index - 1U].type_id() == a_schemas[index].type_id())
        {
            return Result<ComponentValueSchemaRegistry>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidComponentData,
                "Component value schema registry contains a duplicate TypeId"));
        }
    }
    return Result<ComponentValueSchemaRegistry>::success(
        ComponentValueSchemaRegistry(std::move(a_schemas)));
}

const ComponentValueSchema *ComponentValueSchemaRegistry::find(
    schema::TypeId a_typeId) const noexcept
{
    const auto found = std::lower_bound(
        m_schemas.begin(), m_schemas.end(), a_typeId,
        /// @brief Value SchemaのTypeIdを検索値と比較する
        [](const ComponentValueSchema &a_schema,
           schema::TypeId a_value) noexcept
        {
            return a_schema.type_id() < a_value;
        });
    return found != m_schemas.end() && found->type_id() == a_typeId
               ? &*found
               : nullptr;
}

KnownComponentData::KnownComponentData(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion,
    std::vector<KnownFieldData> a_knownFields,
    std::vector<OpaqueFieldData> a_unknownFields) noexcept
    : m_instanceId(std::move(a_instanceId)), m_typeId(a_typeId),
      m_schemaVersion(a_schemaVersion),
      m_knownFields(std::move(a_knownFields)),
      m_unknownFields(std::move(a_unknownFields))
{
}

const ComponentInstanceId &KnownComponentData::instance_id() const noexcept
{
    return m_instanceId;
}

schema::TypeId KnownComponentData::type_id() const noexcept
{
    return m_typeId;
}

schema::SchemaVersion KnownComponentData::schema_version() const noexcept
{
    return m_schemaVersion;
}

std::span<const KnownFieldData> KnownComponentData::known_fields() const noexcept
{
    return m_knownFields;
}

std::span<const OpaqueFieldData> KnownComponentData::unknown_fields() const noexcept
{
    return m_unknownFields;
}

OpaqueComponentData::OpaqueComponentData(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion, std::string a_rawJson) noexcept
    : m_instanceId(std::move(a_instanceId)), m_typeId(a_typeId),
      m_schemaVersion(a_schemaVersion), m_rawJson(std::move(a_rawJson))
{
}

Result<OpaqueComponentData> OpaqueComponentData::create(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion, std::string_view a_rawJson,
    const schema::SchemaRegistry &a_schemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    if (a_rawJson.empty() || !is_valid_utf8(a_rawJson))
    {
        return Result<OpaqueComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidOpaqueData,
            "Opaque component JSON entry must not be empty"));
    }
    auto descriptorResult = a_schemaRegistry.find(a_typeId, a_assertContext);
    if (descriptorResult)
    {
        const auto *descriptor = *descriptorResult.try_value();
        if (a_schemaVersion <= descriptor->version())
        {
            return Result<OpaqueComponentData>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidOpaqueData,
                "Registered component type is opaque only for a future schema version"));
        }
    }
    try
    {
        return Result<OpaqueComponentData>::success(OpaqueComponentData(
            std::move(a_instanceId), a_typeId, a_schemaVersion,
            std::string(a_rawJson)));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

const ComponentInstanceId &OpaqueComponentData::instance_id() const noexcept
{
    return m_instanceId;
}

schema::TypeId OpaqueComponentData::type_id() const noexcept
{
    return m_typeId;
}

schema::SchemaVersion OpaqueComponentData::schema_version() const noexcept
{
    return m_schemaVersion;
}

std::string_view OpaqueComponentData::raw_json() const noexcept
{
    return m_rawJson;
}

SceneComponent::SceneComponent(Storage a_storage) noexcept
    : m_storage(std::move(a_storage))
{
}

SceneComponent SceneComponent::known(KnownComponentData a_data) noexcept
{
    return SceneComponent(Storage(std::in_place_type<KnownComponentData>,
                                  std::move(a_data)));
}

SceneComponent SceneComponent::opaque(OpaqueComponentData a_data) noexcept
{
    return SceneComponent(Storage(std::in_place_type<OpaqueComponentData>,
                                  std::move(a_data)));
}

const ComponentInstanceId &SceneComponent::instance_id() const noexcept
{
    if (const auto *knownData = try_known(); knownData != nullptr)
    {
        return knownData->instance_id();
    }
    return std::get<OpaqueComponentData>(m_storage).instance_id();
}

const KnownComponentData *SceneComponent::try_known() const noexcept
{
    return std::get_if<KnownComponentData>(&m_storage);
}

const OpaqueComponentData *SceneComponent::try_opaque() const noexcept
{
    return std::get_if<OpaqueComponentData>(&m_storage);
}

Result<KnownFieldData> create_known_field(
    schema::FieldId a_id, FieldValue a_value,
    FieldValueKind a_expectedKind,
    const AssertContext &a_assertContext) noexcept
{
    if (a_value.kind() != a_expectedKind)
    {
        return Result<KnownFieldData>::failure(make_scene_error(
            a_assertContext, SceneError::FieldTypeMismatch,
            "Scene field value kind does not match its declared binding"));
    }
    return Result<KnownFieldData>::success(
        KnownFieldData(a_id, std::move(a_value)));
}

Result<ComponentValueSchema> create_component_value_schema(
    schema::TypeId a_typeId, schema::SchemaVersion a_version,
    std::vector<FieldKindBinding> a_fieldKinds,
    const schema::SchemaRegistry &a_schemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    auto descriptorResult = a_schemaRegistry.find(a_typeId, a_assertContext);
    if (!descriptorResult)
    {
        return Result<ComponentValueSchema>::failure(make_scene_error(
            a_assertContext, SceneError::UnknownSchemaType,
            "Component value schema requires a registered TypeId"));
    }
    const auto *descriptor = *descriptorResult.try_value();
    if (descriptor->version() != a_version ||
        descriptor->fields().size() != a_fieldKinds.size())
    {
        return Result<ComponentValueSchema>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Component value schema version or field set does not match M10 schema"));
    }
    try
    {
        std::sort(a_fieldKinds.begin(), a_fieldKinds.end(),
                  /// @brief Field Kind BindingをStable FieldId順へ並べる
                  [](const FieldKindBinding &a_left,
                     const FieldKindBinding &a_right) noexcept
                  {
                      return a_left.id < a_right.id;
                  });
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
    for (std::size_t index = 0U; index < a_fieldKinds.size(); ++index)
    {
        if (!descriptor_has_field(*descriptor, a_fieldKinds[index].id) ||
            (index > 0U &&
             !(a_fieldKinds[index - 1U].id < a_fieldKinds[index].id)))
        {
            return Result<ComponentValueSchema>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidComponentData,
                "Component value schema fields must match M10 schema exactly"));
        }
    }
    return Result<ComponentValueSchema>::success(ComponentValueSchema(
        a_typeId, a_version, std::move(a_fieldKinds)));
}

Result<KnownComponentData> create_known_component(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion,
    std::vector<KnownFieldData> a_knownFields,
    std::vector<OpaqueFieldData> a_unknownFields,
    const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    const auto *valueSchema = a_valueSchemaRegistry.find(a_typeId);
    if (valueSchema == nullptr)
    {
        return Result<KnownComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::UnknownSchemaType,
            "Known scene component requires a registered schema type"));
    }
    if (valueSchema->version() != a_schemaVersion)
    {
        return Result<KnownComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Known scene component schema version or field binding set is invalid"));
    }

    const auto fieldKinds = valueSchema->field_kinds();

    std::sort(a_knownFields.begin(), a_knownFields.end(),
              /// @brief Known FieldをStable FieldId順へ並べる
              [](const KnownFieldData &a_left,
                 const KnownFieldData &a_right) noexcept
              {
                  return a_left.id() < a_right.id();
              });
    std::sort(a_unknownFields.begin(), a_unknownFields.end(),
              /// @brief Unknown FieldをStable FieldId順へ並べる
              [](const OpaqueFieldData &a_left,
                 const OpaqueFieldData &a_right) noexcept
              {
                  return a_left.id() < a_right.id();
              });

    for (std::size_t index = 0U; index < a_knownFields.size(); ++index)
    {
        const auto id = a_knownFields[index].id();
        if (index > 0U && a_knownFields[index - 1U].id() == id)
        {
            return Result<KnownComponentData>::failure(make_scene_error(
                a_assertContext, SceneError::DuplicateFieldId,
                "Known scene component contains a duplicate FieldId"));
        }
        const auto *binding = find_binding(fieldKinds, id);
        if (binding == nullptr || binding->kind != a_knownFields[index].value().kind())
        {
            return Result<KnownComponentData>::failure(make_scene_error(
                a_assertContext,
                binding == nullptr ? SceneError::UnknownSchemaField
                                   : SceneError::FieldTypeMismatch,
                "Known scene field does not match its schema binding"));
        }
    }

    for (std::size_t index = 0U; index < a_unknownFields.size(); ++index)
    {
        const auto id = a_unknownFields[index].id();
        if (find_binding(fieldKinds, id) != nullptr ||
            (index > 0U && a_unknownFields[index - 1U].id() == id) ||
            std::any_of(a_knownFields.begin(), a_knownFields.end(),
                        /// @brief Known FieldとUnknown FieldのIdentity衝突を判定する
                        [id](const KnownFieldData &a_field) noexcept
                        {
                            return a_field.id() == id;
                        }))
        {
            return Result<KnownComponentData>::failure(make_scene_error(
                a_assertContext, SceneError::DuplicateFieldId,
                "Opaque scene field identity conflicts with known or opaque data"));
        }
    }

    return Result<KnownComponentData>::success(KnownComponentData(
        std::move(a_instanceId), a_typeId, a_schemaVersion,
        std::move(a_knownFields), std::move(a_unknownFields)));
}
} // namespace cue::scene
