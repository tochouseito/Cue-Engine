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
    if (a_token.empty() || a_token.size() > 255U)
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
    if (a_rawJson.empty())
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
    const AssertContext &a_assertContext) noexcept
{
    if (a_rawJson.empty())
    {
        return Result<OpaqueComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidOpaqueData,
            "Opaque component JSON entry must not be empty"));
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

Result<KnownComponentData> create_known_component(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion,
    std::vector<KnownFieldData> a_knownFields,
    std::vector<OpaqueFieldData> a_unknownFields,
    const schema::SchemaRegistry &a_schemaRegistry,
    std::span<const FieldKindBinding> a_fieldKinds,
    const AssertContext &a_assertContext) noexcept
{
    auto descriptorResult = a_schemaRegistry.find(a_typeId, a_assertContext);
    if (!descriptorResult)
    {
        return Result<KnownComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::UnknownSchemaType,
            "Known scene component requires a registered schema type"));
    }
    const auto *descriptor = *descriptorResult.try_value();
    if (descriptor->version() != a_schemaVersion ||
        descriptor->fields().size() != a_fieldKinds.size())
    {
        return Result<KnownComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Known scene component schema version or field binding set is invalid"));
    }

    for (std::size_t index = 0U; index < a_fieldKinds.size(); ++index)
    {
        if (!descriptor_has_field(*descriptor, a_fieldKinds[index].id) ||
            (index > 0U &&
             !(a_fieldKinds[index - 1U].id < a_fieldKinds[index].id)))
        {
            return Result<KnownComponentData>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidComponentData,
                "Scene field kind bindings must match schema fields in stable order"));
        }
    }

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
        const auto *binding = find_binding(a_fieldKinds, id);
        if (binding == nullptr || binding->kind != a_knownFields[index].value().kind() ||
            (index > 0U && a_knownFields[index - 1U].id() == id))
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
        if (descriptor_has_field(*descriptor, id) ||
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
