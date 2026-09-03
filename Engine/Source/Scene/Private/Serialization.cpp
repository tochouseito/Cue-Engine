#include <Cue/Scene/Serialization.h>

#include "Json.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/IO/Error.h>
#include <Cue/Math/Scalar.h>
#include <Cue/Scene/Error.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <type_traits>
#include <utility>

namespace
{
using cue::scene::k_maximumSceneBytes;
using cue::scene_private::JsonType;
using cue::scene_private::JsonValue;

/// @brief 予期しないScene Serialization例外をFatal境界へ変換する
[[noreturn]] void terminate_serialization_exception(const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Scene serialization failed unexpectedly");
    std::abort();
}

/// @brief Scene Format Errorを簡潔に生成する
[[nodiscard]] cue::Error format_error(const cue::AssertContext &a_assertContext, cue::scene::SceneError a_code,
                                      std::string_view a_summary) noexcept
{
    return cue::scene::make_scene_error(a_assertContext, a_code, a_summary);
}

/// @brief JSON Objectが指定Memberだけを一度ずつ持つか判定する
template <std::size_t Size>
[[nodiscard]] bool has_exact_members(const JsonValue &a_value,
                                     const std::array<std::string_view, Size> &a_names) noexcept
{
    if (a_value.type != JsonType::Object || a_value.members.size() != a_names.size())
    {
        return false;
    }
    return std::ranges::all_of(a_names,
                               /// @brief 必須MemberがObject内に存在するか判定する
                               [&a_value](std::string_view a_name) noexcept
                               { return cue::scene_private::find_json_member(a_value, a_name) != nullptr; });
}

/// @brief canonical unsigned 32-bit JSON Numberを解析する
[[nodiscard]] bool parse_u32(const JsonValue &a_value, std::uint32_t &a_output) noexcept
{
    if (a_value.type != JsonType::Number || a_value.text.empty() || a_value.text.front() == '-' ||
        a_value.text.front() == '+' || (a_value.text.size() > 1U && a_value.text.front() == '0'))
    {
        return false;
    }
    const auto result = std::from_chars(a_value.text.data(), a_value.text.data() + a_value.text.size(), a_output);
    return result.ec == std::errc{} && result.ptr == a_value.text.data() + a_value.text.size();
}

/// @brief canonical signed 64-bit JSON Numberを解析する
[[nodiscard]] bool parse_i64(const JsonValue &a_value, std::int64_t &a_output) noexcept
{
    if (a_value.type != JsonType::Number || a_value.text.find_first_of(".eE") != std::string::npos)
    {
        return false;
    }
    const auto result = std::from_chars(a_value.text.data(), a_value.text.data() + a_value.text.size(), a_output);
    return result.ec == std::errc{} && result.ptr == a_value.text.data() + a_value.text.size();
}

/// @brief canonical unsigned 64-bit JSON Numberを解析する
[[nodiscard]] bool parse_u64(const JsonValue &a_value, std::uint64_t &a_output) noexcept
{
    if (a_value.type != JsonType::Number || a_value.text.starts_with('-') ||
        a_value.text.find_first_of(".eE") != std::string::npos)
    {
        return false;
    }
    const auto result = std::from_chars(a_value.text.data(), a_value.text.data() + a_value.text.size(), a_output);
    return result.ec == std::errc{} && result.ptr == a_value.text.data() + a_value.text.size();
}

/// @brief finite double JSON Numberを解析する
[[nodiscard]] bool parse_double(const JsonValue &a_value, double &a_output) noexcept
{
    if (a_value.type != JsonType::Number)
    {
        return false;
    }
    const auto result = std::from_chars(a_value.text.data(), a_value.text.data() + a_value.text.size(), a_output,
                                        std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == a_value.text.data() + a_value.text.size() &&
           std::isfinite(a_output);
}

/// @brief Numberをlocale非依存の最短Round-trip表現で追加する
template <typename Value> void append_number(std::string &a_output, Value a_value)
{
    std::array<char, 64U> buffer{};
    std::to_chars_result result{};
    if constexpr (std::is_floating_point_v<Value>)
    {
        result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), a_value, std::chars_format::general,
                               std::numeric_limits<Value>::max_digits10);
    }
    else
    {
        result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), a_value);
    }
    if (result.ec != std::errc{})
    {
        std::abort();
    }
    a_output.append(buffer.data(), result.ptr);
}

/// @brief 16-byte Stable IDをlowercase UUID Textへ追加する
void append_uuid(std::string &a_output, std::span<const std::uint8_t, 16> a_bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    a_output.push_back('"');
    std::size_t position = 0U;
    for (const auto byte : a_bytes)
    {
        if (position == 4U || position == 6U || position == 8U || position == 10U)
        {
            a_output.push_back('-');
        }
        a_output.push_back(digits[(byte >> 4U) & 0x0FU]);
        a_output.push_back(digits[byte & 0x0FU]);
        ++position;
    }
    a_output.push_back('"');
}

/// @brief JSON Rootから正のFormat Versionを取得する
[[nodiscard]] bool read_format_version(const JsonValue &a_root, std::uint32_t &a_version) noexcept
{
    const auto *member = cue::scene_private::find_json_member(a_root, "formatVersion");
    return member != nullptr && parse_u32(*member, a_version) && a_version != 0U;
}

/// @brief FieldValueをKindに対応するJSON Valueへ追加する
void append_field_value(std::string &a_output, const cue::scene::FieldValue &a_value)
{
    using cue::scene::FieldValueKind;
    switch (a_value.kind())
    {
    case FieldValueKind::Boolean:
        a_output.append(*a_value.try_boolean() ? "true" : "false");
        break;
    case FieldValueKind::SignedInteger:
        append_number(a_output, *a_value.try_signed_integer());
        break;
    case FieldValueKind::UnsignedInteger:
        append_number(a_output, *a_value.try_unsigned_integer());
        break;
    case FieldValueKind::FloatingPoint:
        append_number(a_output, *a_value.try_floating_point());
        break;
    case FieldValueKind::String:
        cue::scene_private::append_json_string(a_output, *a_value.try_string());
        break;
    case FieldValueKind::AssetReference:
        cue::scene_private::append_json_string(a_output, a_value.try_asset_reference()->token());
        break;
    }
}

/// @brief Transformを固定Member順のJSONへ追加する
void append_transform(std::string &a_output, const cue::math::Transform &a_transform)
{
    const auto translation = a_transform.translation();
    const auto rotation = a_transform.rotation();
    const auto scale = a_transform.scale();
    a_output.append("{\"translation\":[");
    append_number(a_output, translation.x);
    a_output.push_back(',');
    append_number(a_output, translation.y);
    a_output.push_back(',');
    append_number(a_output, translation.z);
    a_output.append("],\"rotation\":[");
    append_number(a_output, rotation.x);
    a_output.push_back(',');
    append_number(a_output, rotation.y);
    a_output.push_back(',');
    append_number(a_output, rotation.z);
    a_output.push_back(',');
    append_number(a_output, rotation.w);
    a_output.append("],\"scale\":[");
    append_number(a_output, scale.x);
    a_output.push_back(',');
    append_number(a_output, scale.y);
    a_output.push_back(',');
    append_number(a_output, scale.z);
    a_output.append("]}");
}

/// @brief Scene上限を超えず指定Byte数を追加できるか返す
[[nodiscard]] bool can_append_scene_bytes(const std::string &a_output,
                                          std::size_t a_byteCount) noexcept
{
    return a_output.size() <= k_maximumSceneBytes &&
           a_byteCount <= k_maximumSceneBytes - a_output.size();
}

/// @brief Scene Componentを上限内で固定Wire Schemaへ追加する
[[nodiscard]] bool append_component(
    std::string &a_output, const cue::scene::SceneComponent &a_component)
{
    const auto *known = a_component.try_known();
    const auto *opaque = a_component.try_opaque();
    if (opaque != nullptr && opaque->is_complete_entry())
    {
        if (!can_append_scene_bytes(a_output, opaque->raw_json().size()))
        {
            return false;
        }
        a_output.append(opaque->raw_json());
        return true;
    }
    a_output.append("{\"componentInstanceId\":");
    append_uuid(a_output, a_component.instance_id().bytes());
    const auto typeId = known != nullptr ? known->type_id() : opaque->type_id();
    const auto version = known != nullptr ? known->schema_version() : opaque->schema_version();
    a_output.append(",\"typeId\":");
    append_uuid(a_output, typeId.bytes());
    a_output.append(",\"schemaVersion\":");
    append_number(a_output, version.value());
    if (known != nullptr)
    {
        a_output.append(",\"fields\":[");
        const auto knownFields = known->known_fields();
        const auto unknownFields = known->unknown_fields();
        std::size_t knownIndex = 0U;
        std::size_t unknownIndex = 0U;
        bool first = true;
        while (knownIndex < knownFields.size() || unknownIndex < unknownFields.size())
        {
            if (!first)
            {
                a_output.push_back(',');
            }
            first = false;
            const bool useKnown =
                unknownIndex == unknownFields.size() ||
                (knownIndex < knownFields.size() && knownFields[knownIndex].id() < unknownFields[unknownIndex].id());
            a_output.append("{\"fieldId\":");
            append_number(a_output,
                          useKnown ? knownFields[knownIndex].id().value() : unknownFields[unknownIndex].id().value());
            a_output.append(",\"value\":");
            if (a_output.size() > k_maximumSceneBytes)
            {
                return false;
            }
            if (useKnown)
            {
                append_field_value(a_output, knownFields[knownIndex].value());
                ++knownIndex;
            }
            else
            {
                if (!can_append_scene_bytes(
                        a_output, unknownFields[unknownIndex].raw_json().size()))
                {
                    return false;
                }
                a_output.append(unknownFields[unknownIndex].raw_json());
                ++unknownIndex;
            }
            if (a_output.size() > k_maximumSceneBytes)
            {
                return false;
            }
            a_output.push_back('}');
        }
        a_output.push_back(']');
    }
    else
    {
        a_output.append(",\"payload\":");
        if (!can_append_scene_bytes(a_output, opaque->raw_json().size()))
        {
            return false;
        }
        a_output.append(opaque->raw_json());
    }
    a_output.push_back('}');
    return a_output.size() <= k_maximumSceneBytes;
}

/// @brief JSON Number Arrayを固定個数のfloatへ解析する
template <std::size_t Size>
[[nodiscard]] bool parse_float_array(const JsonValue &a_value, std::array<float, Size> &a_output) noexcept
{
    if (a_value.type != JsonType::Array || a_value.elements.size() != Size)
    {
        return false;
    }
    for (std::size_t index = 0U; index < Size; ++index)
    {
        double parsed = 0.0;
        if (!parse_double(a_value.elements[index], parsed) || parsed < -std::numeric_limits<float>::max() ||
            parsed > std::numeric_limits<float>::max())
        {
            return false;
        }
        a_output[index] = static_cast<float>(parsed);
    }
    return true;
}

/// @brief 固定Wire Transformを検証済みMath Transformへ解析する
[[nodiscard]] cue::Result<cue::math::Transform> parse_transform(const JsonValue &a_value,
                                                                const cue::AssertContext &a_assertContext) noexcept
{
    constexpr std::array names = {std::string_view("translation"), std::string_view("rotation"),
                                  std::string_view("scale")};
    if (!has_exact_members(a_value, names))
    {
        return cue::Result<cue::math::Transform>::failure(format_error(
            a_assertContext, cue::scene::SceneError::InvalidFormat, "Scene transform has missing or unknown members"));
    }
    std::array<float, 3U> translation{};
    std::array<float, 4U> rotation{};
    std::array<float, 3U> scale{};
    if (!parse_float_array(*cue::scene_private::find_json_member(a_value, "translation"), translation) ||
        !parse_float_array(*cue::scene_private::find_json_member(a_value, "rotation"), rotation) ||
        !parse_float_array(*cue::scene_private::find_json_member(a_value, "scale"), scale))
    {
        return cue::Result<cue::math::Transform>::failure(
            format_error(a_assertContext, cue::scene::SceneError::InvalidFormat,
                         "Scene transform must contain finite float arrays"));
    }
    auto tolerance = cue::math::Tolerance::create(a_assertContext.fatal_handler(), 0.00001F, 0.00001F);
    if (!tolerance)
    {
        return cue::Result<cue::math::Transform>::failure(std::move(*tolerance.try_error()));
    }
    return cue::math::Transform::create(
        a_assertContext.fatal_handler(), {translation[0], translation[1], translation[2]},
        {rotation[0], rotation[1], rotation[2], rotation[3]}, {scale[0], scale[1], scale[2]}, *tolerance.try_value());
}

/// @brief Schema Kindに従って既知Field Valueを解析する
[[nodiscard]] cue::Result<cue::scene::FieldValue> parse_field_value(const JsonValue &a_value,
                                                                    cue::scene::FieldValueKind a_kind,
                                                                    const cue::AssertContext &a_assertContext) noexcept
{
    using cue::scene::FieldValue;
    using cue::scene::FieldValueKind;
    switch (a_kind)
    {
    case FieldValueKind::Boolean:
        if (a_value.type == JsonType::Boolean)
        {
            return cue::Result<FieldValue>::success(FieldValue::boolean(a_value.boolean));
        }
        break;
    case FieldValueKind::SignedInteger:
    {
        std::int64_t value = 0;
        if (parse_i64(a_value, value))
        {
            return cue::Result<FieldValue>::success(FieldValue::signed_integer(value));
        }
        break;
    }
    case FieldValueKind::UnsignedInteger:
    {
        std::uint64_t value = 0U;
        if (parse_u64(a_value, value))
        {
            return cue::Result<FieldValue>::success(FieldValue::unsigned_integer(value));
        }
        break;
    }
    case FieldValueKind::FloatingPoint:
    {
        double value = 0.0;
        if (parse_double(a_value, value))
        {
            return FieldValue::floating_point(value, a_assertContext);
        }
        break;
    }
    case FieldValueKind::String:
        if (a_value.type == JsonType::String)
        {
            return FieldValue::string(a_value.text, a_assertContext);
        }
        break;
    case FieldValueKind::AssetReference:
        if (a_value.type == JsonType::String)
        {
            auto reference = cue::scene::AssetReferenceValue::create(a_value.text, a_assertContext);
            if (reference)
            {
                return cue::Result<FieldValue>::success(FieldValue::asset_reference(std::move(*reference.try_value())));
            }
            return cue::Result<FieldValue>::failure(std::move(*reference.try_error()));
        }
        break;
    }
    return cue::Result<FieldValue>::failure(format_error(a_assertContext, cue::scene::SceneError::FieldTypeMismatch,
                                                         "Scene field JSON value does not match its schema kind"));
}

/// @brief IO ErrorをScene Storage分類へ再分類する
[[nodiscard]] cue::Error storage_error(const cue::AssertContext &a_assertContext, cue::scene::SceneError a_code,
                                       std::string_view a_summary, cue::Error a_cause) noexcept
{
    auto code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.Scene", static_cast<std::int64_t>(a_code));
    return cue::Error::reclassify(a_assertContext.fatal_handler(), std::move(code), a_summary, std::move(a_cause));
}
} // namespace

namespace cue::scene
{
class SceneDocumentSerializationAccess final
{
  public:
    /// @brief Parse済みObject Dataを公開Mutationの反復なしで所有値へ変換する
    [[nodiscard]] static SceneObject create_object(
        ObjectId a_id, std::string a_name, bool a_isActive,
        std::optional<ObjectId> a_parentId, math::Transform a_transform,
        std::vector<SceneComponent> a_components) noexcept
    {
        return SceneObject(std::move(a_id), std::move(a_name), a_isActive,
                           std::move(a_parentId), std::move(a_transform),
                           std::move(a_components));
    }

    /// @brief 一括検証済みObject集合をDocumentへ移しStable ID Indexを一度だけ構築する
    static void set_objects(SceneDocument &a_document,
                            std::vector<SceneObject> a_objects) noexcept
    {
        a_document.m_objects = std::move(a_objects);
        a_document.rebuild_index();
    }

    /// @brief Parse済みExtension JSONをSceneDocumentへ所有させる
    static void set_extensions(SceneDocument &a_document, std::string a_json) noexcept
    {
        a_document.m_extensionsJson = std::move(a_json);
    }

    /// @brief Parse済みの完全な未知Component EntryをLossless Dataへ変換する
    [[nodiscard]] static Result<OpaqueComponentData> create_opaque_entry(ComponentInstanceId a_instanceId,
                                                                         schema::TypeId a_typeId,
                                                                         schema::SchemaVersion a_schemaVersion,
                                                                         std::string_view a_rawJson,
                                                                         const schema::SchemaRegistry &a_schemaRegistry,
                                                                         const AssertContext &a_assertContext) noexcept
    {
        return OpaqueComponentData::create_complete_entry(std::move(a_instanceId), a_typeId, a_schemaVersion, a_rawJson,
                                                          a_schemaRegistry, a_assertContext);
    }
};

Result<void> SceneMigrationRegistry::add_step(std::uint32_t a_fromVersion, SceneMigrationFunction a_function,
                                              const AssertContext &a_assertContext) noexcept
{
    if (a_fromVersion == 0U || a_fromVersion == std::numeric_limits<std::uint32_t>::max() || a_function == nullptr ||
        std::ranges::any_of(m_steps,
                            /// @brief 同じfromVersionが登録済みか判定する
                            [a_fromVersion](const Step &a_step) noexcept
                            { return a_step.fromVersion == a_fromVersion; }))
    {
        return Result<void>::failure(format_error(a_assertContext, SceneError::InvalidFormat,
                                                  "Scene migration step must be unique N to N plus one"));
    }
    try
    {
        m_steps.push_back({a_fromVersion, a_function});
        std::sort(m_steps.begin(), m_steps.end(),
                  /// @brief Migration StepをfromVersion順へ並べる
                  [](const Step &a_left, const Step &a_right) noexcept
                  { return a_left.fromVersion < a_right.fromVersion; });
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
    return Result<void>::success();
}

Result<std::string> SceneMigrationRegistry::migrate(std::string_view a_source, std::uint32_t a_fromVersion,
                                                    std::uint32_t a_targetVersion,
                                                    const AssertContext &a_assertContext) const noexcept
{
    if (a_fromVersion == 0U || a_targetVersion < a_fromVersion || a_source.size() > k_maximumSceneBytes)
    {
        return Result<std::string>::failure(format_error(a_assertContext, SceneError::MigrationFailed,
                                                         "Scene migration source or version range is invalid"));
    }
    try
    {
        std::string current(a_source);
        JsonValue initialRoot;
        std::string_view initialParseError;
        std::uint32_t initialVersion = 0U;
        if (!cue::scene_private::parse_json_document(current, initialRoot, initialParseError) ||
            !read_format_version(initialRoot, initialVersion) || initialVersion != a_fromVersion)
        {
            return Result<std::string>::failure(
                format_error(a_assertContext, SceneError::MigrationFailed,
                             "Scene migration source does not match the declared format version"));
        }
        for (std::uint32_t version = a_fromVersion; version < a_targetVersion; ++version)
        {
            const auto found =
                std::ranges::find_if(m_steps,
                                     /// @brief 現在VersionのMigration Stepを検索する
                                     [version](const Step &a_step) noexcept { return a_step.fromVersion == version; });
            if (found == m_steps.end())
            {
                return Result<std::string>::failure(format_error(a_assertContext, SceneError::MissingMigrationStep,
                                                                 "Scene migration chain has a missing step"));
            }
            auto migrated = found->function(current, a_assertContext);
            if (!migrated)
            {
                return Result<std::string>::failure(std::move(*migrated.try_error()));
            }
            JsonValue root;
            std::string_view parseError;
            std::uint32_t outputVersion = 0U;
            if (migrated.try_value()->size() > k_maximumSceneBytes ||
                !cue::scene_private::parse_json_document(*migrated.try_value(), root, parseError) ||
                !read_format_version(root, outputVersion) || outputVersion != version + 1U)
            {
                return Result<std::string>::failure(
                    format_error(a_assertContext, SceneError::MigrationFailed,
                                 "Scene migration did not produce the next format version"));
            }
            current = std::move(*migrated.try_value());
            if (version == a_targetVersion - 1U)
            {
                break;
            }
        }
        return Result<std::string>::success(std::move(current));
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
}

Result<void> ComponentMigrationRegistry::add_step(schema::TypeId a_typeId, std::uint32_t a_fromVersion,
                                                  ComponentMigrationFunction a_function,
                                                  const AssertContext &a_assertContext) noexcept
{
    if (a_fromVersion == 0U || a_fromVersion == std::numeric_limits<std::uint32_t>::max() || a_function == nullptr ||
        std::ranges::any_of(m_steps,
                            /// @brief 同じTypeとfromVersionが登録済みか判定する
                            [a_typeId, a_fromVersion](const Step &a_step) noexcept
                            { return a_step.typeId == a_typeId && a_step.fromVersion == a_fromVersion; }))
    {
        return Result<void>::failure(format_error(a_assertContext, SceneError::InvalidFormat,
                                                  "Component migration step must be unique N to N plus one"));
    }
    try
    {
        m_steps.push_back({a_typeId, a_fromVersion, a_function});
        std::sort(m_steps.begin(), m_steps.end(),
                  /// @brief Component Migration StepをTypeとfromVersion順へ並べる
                  [](const Step &a_left, const Step &a_right) noexcept
                  {
                      if (a_left.typeId != a_right.typeId)
                      {
                          return a_left.typeId < a_right.typeId;
                      }
                      return a_left.fromVersion < a_right.fromVersion;
                  });
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
    return Result<void>::success();
}

Result<std::string> ComponentMigrationRegistry::migrate(schema::TypeId a_typeId, std::string_view a_fieldsJson,
                                                        std::uint32_t a_fromVersion, std::uint32_t a_targetVersion,
                                                        const AssertContext &a_assertContext) const noexcept
{
    if (a_fromVersion == 0U || a_targetVersion < a_fromVersion || a_fieldsJson.size() > k_maximumSceneBytes)
    {
        return Result<std::string>::failure(format_error(a_assertContext, SceneError::MigrationFailed,
                                                         "Component migration source or version range is invalid"));
    }
    try
    {
        std::string current(a_fieldsJson);
        JsonValue initialFields;
        std::string_view initialParseError;
        if (!cue::scene_private::parse_json_document(current, initialFields, initialParseError) ||
            initialFields.type != JsonType::Array)
        {
            return Result<std::string>::failure(
                format_error(a_assertContext, SceneError::MigrationFailed,
                             "Component migration source must be a bounded JSON field array"));
        }
        for (std::uint32_t version = a_fromVersion; version < a_targetVersion; ++version)
        {
            const auto found =
                std::ranges::find_if(m_steps,
                                     /// @brief 現在TypeとVersionのComponent Migration Stepを検索する
                                     [a_typeId, version](const Step &a_step) noexcept
                                     { return a_step.typeId == a_typeId && a_step.fromVersion == version; });
            if (found == m_steps.end())
            {
                return Result<std::string>::failure(format_error(a_assertContext, SceneError::MissingMigrationStep,
                                                                 "Component migration chain has a missing step"));
            }
            auto migrated = found->function(current, a_assertContext);
            if (!migrated)
            {
                return Result<std::string>::failure(std::move(*migrated.try_error()));
            }
            JsonValue fields;
            std::string_view parseError;
            if (migrated.try_value()->size() > k_maximumSceneBytes ||
                !cue::scene_private::parse_json_document(*migrated.try_value(), fields, parseError) ||
                fields.type != JsonType::Array)
            {
                return Result<std::string>::failure(
                    format_error(a_assertContext, SceneError::MigrationFailed,
                                 "Component migration did not produce a bounded JSON field array"));
            }
            current = std::move(*migrated.try_value());
            if (version == a_targetVersion - 1U)
            {
                break;
            }
        }
        return Result<std::string>::success(std::move(current));
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
}

SceneLoadResult::SceneLoadResult(SceneDocument a_document, std::uint32_t a_sourceFormatVersion) noexcept
    : m_document(std::move(a_document)), m_sourceFormatVersion(a_sourceFormatVersion)
{
}

SceneDocument &SceneLoadResult::document() noexcept
{
    return m_document;
}

const SceneDocument &SceneLoadResult::document() const noexcept
{
    return m_document;
}

std::uint32_t SceneLoadResult::source_format_version() const noexcept
{
    return m_sourceFormatVersion;
}

bool SceneLoadResult::migration_required() const noexcept
{
    return m_sourceFormatVersion != k_currentSceneFormatVersion;
}

SceneSaveOutcome::SceneSaveOutcome(SceneSaveStatus a_status, std::optional<Error> a_error) noexcept
    : m_status(a_status), m_error(std::move(a_error))
{
}

SceneSaveStatus SceneSaveOutcome::status() const noexcept
{
    return m_status;
}

const Error *SceneSaveOutcome::try_error() const noexcept
{
    return m_error ? &*m_error : nullptr;
}

SceneSaveOutcome SceneSaveOutcome::committed() noexcept
{
    return SceneSaveOutcome(SceneSaveStatus::Committed, std::nullopt);
}

SceneSaveOutcome SceneSaveOutcome::not_published(Error a_error) noexcept
{
    return SceneSaveOutcome(SceneSaveStatus::NotPublished, std::optional<Error>(std::move(a_error)));
}

SceneSaveOutcome SceneSaveOutcome::durability_unknown(Error a_error) noexcept
{
    return SceneSaveOutcome(SceneSaveStatus::PublishedButDurabilityUnknown, std::optional<Error>(std::move(a_error)));
}

SceneSaveOutcome SceneSaveOutcome::verification_failed(Error a_error) noexcept
{
    return SceneSaveOutcome(SceneSaveStatus::PublishedButVerificationFailed, std::optional<Error>(std::move(a_error)));
}

Result<std::string> serialize_scene_document(const SceneDocument &a_document,
                                             const AssertContext &a_assertContext) noexcept
{
    auto validation = a_document.validate();
    if (!validation)
    {
        return Result<std::string>::failure(std::move(*validation.try_error()));
    }
    try
    {
        std::string output;
        output.append("{\"formatVersion\":1,\"sceneAssetId\":");
        append_uuid(output, a_document.scene_asset_id().bytes());
        output.append(",\"objects\":[");
        const auto objects = a_document.objects();
        std::vector<const SceneObject *> orderedObjects;
        orderedObjects.reserve(objects.size());
        for (const auto &object : objects)
        {
            if (!cue::scene_private::is_valid_json_string_text(object.name()))
            {
                return Result<std::string>::failure(
                    format_error(a_assertContext, SceneError::InvalidFormat,
                                 "Scene object name must be valid UTF-8 within 256 KiB"));
            }
            orderedObjects.push_back(&object);
        }
        std::sort(orderedObjects.begin(), orderedObjects.end(),
                  /// @brief Scene Objectを永続ObjectId順へ並べる
                  [](const SceneObject *a_left, const SceneObject *a_right) noexcept
                  { return a_left->id() < a_right->id(); });
        for (std::size_t index = 0U; index < orderedObjects.size(); ++index)
        {
            if (index > 0U)
            {
                output.push_back(',');
            }
            const auto &object = *orderedObjects[index];
            output.append("{\"objectId\":");
            append_uuid(output, object.id().bytes());
            output.append(",\"name\":");
            cue::scene_private::append_json_string(output, object.name());
            output.append(",\"active\":");
            if (output.size() > k_maximumSceneBytes)
            {
                return Result<std::string>::failure(
                    format_error(a_assertContext, SceneError::InvalidFormat,
                                 "Serialized scene exceeds 16 MiB"));
            }
            output.append(object.is_active() ? "true" : "false");
            output.append(",\"parentObjectId\":");
            if (object.try_parent_id() == nullptr)
            {
                output.append("null");
            }
            else
            {
                append_uuid(output, object.try_parent_id()->bytes());
            }
            output.append(",\"transform\":");
            append_transform(output, object.transform());
            output.append(",\"components\":[");
            const auto components = object.components();
            for (std::size_t componentIndex = 0U; componentIndex < components.size(); ++componentIndex)
            {
                if (componentIndex > 0U)
                {
                    output.push_back(',');
                }
                if (!append_component(output, components[componentIndex]))
                {
                    return Result<std::string>::failure(
                        format_error(a_assertContext, SceneError::InvalidFormat,
                                     "Serialized scene exceeds 16 MiB"));
                }
            }
            output.append("]}");
        }
        output.append("],\"extensions\":");
        if (!can_append_scene_bytes(output,
                                    a_document.extensions_json().size()))
        {
            return Result<std::string>::failure(
                format_error(a_assertContext, SceneError::InvalidFormat,
                             "Serialized scene exceeds 16 MiB"));
        }
        output.append(a_document.extensions_json());
        output.push_back('}');
        if (output.size() > k_maximumSceneBytes)
        {
            return Result<std::string>::failure(
                format_error(a_assertContext, SceneError::InvalidFormat, "Serialized scene exceeds 16 MiB"));
        }
        JsonValue verificationTree;
        std::string_view verificationError;
        if (!cue::scene_private::parse_json_document(
                output, verificationTree, verificationError))
        {
            return Result<std::string>::failure(format_error(
                a_assertContext, SceneError::InvalidFormat,
                "Serialized scene exceeds JSON parser resource limits"));
        }
        return Result<std::string>::success(std::move(output));
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
}

} // namespace cue::scene

namespace
{
/// @brief Parse済みParent Graphを一回走査して参照、Cycle、Depthを検証する
[[nodiscard]] cue::Result<void> validate_scene_hierarchy(
    std::span<const cue::scene::SceneObject> a_objects,
    const std::map<cue::scene::ObjectId, std::size_t> &a_objectIndex,
    const cue::AssertContext &a_assertContext) noexcept
{
    try
    {
        std::vector<std::uint8_t> states(a_objects.size(), 0U);
        std::vector<std::size_t> depths(a_objects.size(), 0U);
        std::vector<std::size_t> chain;
        chain.reserve(a_objects.size());
        for (std::size_t start = 0U; start < a_objects.size(); ++start)
        {
            if (states[start] == 2U)
            {
                continue;
            }
            chain.clear();
            std::size_t current = start;
            bool reachedRoot = false;
            while (states[current] == 0U)
            {
                states[current] = 1U;
                chain.push_back(current);
                const auto *parentId = a_objects[current].try_parent_id();
                if (parentId == nullptr)
                {
                    reachedRoot = true;
                    break;
                }
                const auto parent = a_objectIndex.find(*parentId);
                if (parent == a_objectIndex.end())
                {
                    return cue::Result<void>::failure(format_error(
                        a_assertContext, cue::scene::SceneError::DanglingParent,
                        "Scene object has a dangling parent identity"));
                }
                current = parent->second;
            }
            if (!reachedRoot && states[current] == 1U)
            {
                return cue::Result<void>::failure(format_error(
                    a_assertContext, cue::scene::SceneError::HierarchyCycle,
                    "Scene object hierarchy contains a cycle"));
            }
            std::size_t depth = reachedRoot ? 0U : depths[current];
            for (auto iterator = chain.rbegin(); iterator != chain.rend();
                 ++iterator)
            {
                ++depth;
                if (depth > cue::scene::SceneDocument::maximum_hierarchy_depth())
                {
                    return cue::Result<void>::failure(format_error(
                        a_assertContext,
                        cue::scene::SceneError::HierarchyDepthExceeded,
                        "Scene object hierarchy exceeds the supported depth"));
                }
                depths[*iterator] = depth;
                states[*iterator] = 2U;
            }
        }
        return cue::Result<void>::success();
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
}

/// @brief Value Schema内のFieldId対応Kindを検索する
[[nodiscard]] const cue::scene::FieldKindBinding *find_field_binding(const cue::scene::ComponentValueSchema &a_schema,
                                                                     cue::schema::FieldId a_id) noexcept
{
    const auto fields = a_schema.field_kinds();
    const auto found = std::ranges::find_if(fields,
                                            /// @brief Field BindingのIdentityが一致するか判定する
                                            [a_id](const cue::scene::FieldKindBinding &a_binding) noexcept
                                            { return a_binding.id == a_id; });
    return found == fields.end() ? nullptr : &*found;
}

/// @brief 固定Wire Component Entryを既知またはOpaque Dataへ解析する
[[nodiscard]] cue::Result<cue::scene::SceneComponent> parse_component(
    std::string_view a_json, const JsonValue &a_value, const cue::schema::SchemaRegistry &a_schemaRegistry,
    const cue::scene::ComponentValueSchemaRegistry &a_valueSchemaRegistry,
    const cue::scene::ComponentMigrationRegistry &a_componentMigrations,
    const cue::AssertContext &a_assertContext) noexcept
{
    if (a_value.type != JsonType::Object)
    {
        return cue::Result<cue::scene::SceneComponent>::failure(format_error(
            a_assertContext, cue::scene::SceneError::InvalidFormat, "Scene component entry must be an object"));
    }
    const auto *instanceMember = cue::scene_private::find_json_member(a_value, "componentInstanceId");
    const auto *typeMember = cue::scene_private::find_json_member(a_value, "typeId");
    const auto *versionMember = cue::scene_private::find_json_member(a_value, "schemaVersion");
    if (instanceMember == nullptr || typeMember == nullptr || versionMember == nullptr ||
        instanceMember->type != JsonType::String || typeMember->type != JsonType::String)
    {
        return cue::Result<cue::scene::SceneComponent>::failure(format_error(
            a_assertContext, cue::scene::SceneError::InvalidFormat, "Scene component identity metadata is missing"));
    }
    auto instanceId = cue::scene::ComponentInstanceId::parse(instanceMember->text, a_assertContext);
    auto typeId = cue::schema::TypeId::parse(typeMember->text, a_assertContext);
    std::uint32_t versionNumber = 0U;
    auto schemaVersion = cue::schema::SchemaVersion::create(
        parse_u32(*versionMember, versionNumber) ? versionNumber : 0U, a_assertContext);
    if (!instanceId || !typeId || !schemaVersion)
    {
        return cue::Result<cue::scene::SceneComponent>::failure(format_error(
            a_assertContext, cue::scene::SceneError::InvalidFormat, "Scene component identity metadata is invalid"));
    }

    auto descriptor = a_schemaRegistry.find(*typeId.try_value(), a_assertContext);
    const bool isUnknownType = !descriptor;
    const bool isFutureVersion = descriptor && *schemaVersion.try_value() > (*descriptor.try_value())->version();
    if (isUnknownType || isFutureVersion)
    {
        if (a_value.end <= a_value.begin)
        {
            return cue::Result<cue::scene::SceneComponent>::failure(format_error(
                a_assertContext, cue::scene::SceneError::InvalidFormat, "Opaque scene component entry is invalid"));
        }
        auto opaque = cue::scene::SceneDocumentSerializationAccess::create_opaque_entry(
            std::move(*instanceId.try_value()), *typeId.try_value(), *schemaVersion.try_value(),
            a_json.substr(a_value.begin, a_value.end - a_value.begin), a_schemaRegistry, a_assertContext);
        if (!opaque)
        {
            return cue::Result<cue::scene::SceneComponent>::failure(std::move(*opaque.try_error()));
        }
        return cue::Result<cue::scene::SceneComponent>::success(
            cue::scene::SceneComponent::opaque(std::move(*opaque.try_value())));
    }

    constexpr std::array knownNames = {std::string_view("componentInstanceId"), std::string_view("typeId"),
                                       std::string_view("schemaVersion"), std::string_view("fields")};
    const auto *valueSchema = a_valueSchemaRegistry.find(*typeId.try_value());
    const auto *fieldsMember = cue::scene_private::find_json_member(a_value, "fields");
    if (!has_exact_members(a_value, knownNames) || fieldsMember == nullptr || fieldsMember->type != JsonType::Array ||
        valueSchema == nullptr || !a_valueSchemaRegistry.is_bound_to(a_schemaRegistry))
    {
        return cue::Result<cue::scene::SceneComponent>::failure(
            format_error(a_assertContext, cue::scene::SceneError::InvalidFormat,
                         "Known scene component fields or value schema are invalid"));
    }

    std::string migratedFieldsStorage;
    JsonValue migratedFields;
    std::string_view fieldsJson = a_json;
    if (*schemaVersion.try_value() < (*descriptor.try_value())->version())
    {
        const auto sourceFields = a_json.substr(fieldsMember->begin, fieldsMember->end - fieldsMember->begin);
        auto migrated =
            a_componentMigrations.migrate(*typeId.try_value(), sourceFields, schemaVersion.try_value()->value(),
                                          (*descriptor.try_value())->version().value(), a_assertContext);
        if (!migrated)
        {
            return cue::Result<cue::scene::SceneComponent>::failure(std::move(*migrated.try_error()));
        }
        migratedFieldsStorage = std::move(*migrated.try_value());
        std::string_view parseError;
        if (!cue::scene_private::parse_json_document(migratedFieldsStorage, migratedFields, parseError) ||
            migratedFields.type != JsonType::Array)
        {
            return cue::Result<cue::scene::SceneComponent>::failure(
                format_error(a_assertContext, cue::scene::SceneError::MigrationFailed,
                             "Component migration did not produce a valid field array"));
        }
        fieldsMember = &migratedFields;
        fieldsJson = migratedFieldsStorage;
    }
    std::vector<cue::scene::KnownFieldData> knownFields;
    std::vector<cue::scene::OpaqueFieldData> unknownFields;
    try
    {
        for (const auto &field : fieldsMember->elements)
        {
            constexpr std::array fieldNames = {std::string_view("fieldId"), std::string_view("value")};
            const auto *idMember = cue::scene_private::find_json_member(field, "fieldId");
            const auto *valueMember = cue::scene_private::find_json_member(field, "value");
            std::uint32_t fieldNumber = 0U;
            if (!has_exact_members(field, fieldNames) || idMember == nullptr || valueMember == nullptr ||
                !parse_u32(*idMember, fieldNumber))
            {
                return cue::Result<cue::scene::SceneComponent>::failure(format_error(
                    a_assertContext, cue::scene::SceneError::InvalidFormat, "Scene field entry is invalid"));
            }
            auto fieldId = cue::schema::FieldId::create(fieldNumber, a_assertContext);
            if (!fieldId)
            {
                return cue::Result<cue::scene::SceneComponent>::failure(std::move(*fieldId.try_error()));
            }
            const auto *binding = find_field_binding(*valueSchema, *fieldId.try_value());
            if (binding != nullptr)
            {
                auto parsedValue = parse_field_value(*valueMember, binding->kind, a_assertContext);
                if (!parsedValue)
                {
                    return cue::Result<cue::scene::SceneComponent>::failure(std::move(*parsedValue.try_error()));
                }
                auto knownField = cue::scene::create_known_field(
                    *fieldId.try_value(), std::move(*parsedValue.try_value()), binding->kind, a_assertContext);
                if (!knownField)
                {
                    return cue::Result<cue::scene::SceneComponent>::failure(std::move(*knownField.try_error()));
                }
                knownFields.push_back(std::move(*knownField.try_value()));
            }
            else
            {
                auto opaqueField = cue::scene::OpaqueFieldData::create(
                    *fieldId.try_value(), fieldsJson.substr(valueMember->begin, valueMember->end - valueMember->begin),
                    a_assertContext);
                if (!opaqueField)
                {
                    return cue::Result<cue::scene::SceneComponent>::failure(std::move(*opaqueField.try_error()));
                }
                unknownFields.push_back(std::move(*opaqueField.try_value()));
            }
        }
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
    auto known = cue::scene::create_known_component(
        std::move(*instanceId.try_value()), *typeId.try_value(), (*descriptor.try_value())->version(),
        std::move(knownFields), std::move(unknownFields), a_schemaRegistry, a_valueSchemaRegistry, a_assertContext);
    if (!known)
    {
        return cue::Result<cue::scene::SceneComponent>::failure(std::move(*known.try_error()));
    }
    return cue::Result<cue::scene::SceneComponent>::success(
        cue::scene::SceneComponent::known(std::move(*known.try_value())));
}

/// @brief Current v1 JSON Treeから完全なSceneDocumentを構築する
[[nodiscard]] cue::Result<cue::scene::SceneDocument> parse_current_document(
    std::string_view a_json, const JsonValue &a_root, const cue::schema::SchemaRegistry &a_schemaRegistry,
    const cue::scene::ComponentValueSchemaRegistry &a_valueSchemaRegistry,
    const cue::scene::ComponentMigrationRegistry &a_componentMigrations,
    const cue::AssertContext &a_assertContext) noexcept
{
    constexpr std::array rootNames = {std::string_view("formatVersion"), std::string_view("sceneAssetId"),
                                      std::string_view("objects"), std::string_view("extensions")};
    const auto *sceneIdMember = cue::scene_private::find_json_member(a_root, "sceneAssetId");
    const auto *objectsMember = cue::scene_private::find_json_member(a_root, "objects");
    const auto *extensionsMember = cue::scene_private::find_json_member(a_root, "extensions");
    if (!has_exact_members(a_root, rootNames) || sceneIdMember == nullptr || sceneIdMember->type != JsonType::String ||
        objectsMember == nullptr || objectsMember->type != JsonType::Array || extensionsMember == nullptr ||
        extensionsMember->type != JsonType::Object)
    {
        return cue::Result<cue::scene::SceneDocument>::failure(
            format_error(a_assertContext, cue::scene::SceneError::InvalidFormat,
                         "Scene v1 envelope has missing or unknown members"));
    }
    auto sceneId = cue::scene::SceneAssetId::parse(sceneIdMember->text, a_assertContext);
    if (!sceneId)
    {
        return cue::Result<cue::scene::SceneDocument>::failure(std::move(*sceneId.try_error()));
    }
    auto document = cue::scene::SceneDocument::create(std::move(*sceneId.try_value()), a_assertContext);
    std::vector<cue::scene::SceneObject> parsedObjects;
    std::map<cue::scene::ObjectId, std::size_t> objectIndex;
    std::set<cue::scene::ComponentInstanceId> componentIds;
    std::size_t retainedComponentBytes = 0U;
    try
    {
        parsedObjects.reserve(objectsMember->elements.size());
        for (const auto &object : objectsMember->elements)
        {
            constexpr std::array objectNames = {std::string_view("objectId"),  std::string_view("name"),
                                                std::string_view("active"),    std::string_view("parentObjectId"),
                                                std::string_view("transform"), std::string_view("components")};
            const auto *idMember = cue::scene_private::find_json_member(object, "objectId");
            const auto *nameMember = cue::scene_private::find_json_member(object, "name");
            const auto *activeMember = cue::scene_private::find_json_member(object, "active");
            const auto *parentMember = cue::scene_private::find_json_member(object, "parentObjectId");
            const auto *transformMember = cue::scene_private::find_json_member(object, "transform");
            const auto *componentsMember = cue::scene_private::find_json_member(object, "components");
            if (!has_exact_members(object, objectNames) || idMember == nullptr || idMember->type != JsonType::String ||
                nameMember == nullptr || nameMember->type != JsonType::String || activeMember == nullptr ||
                activeMember->type != JsonType::Boolean || parentMember == nullptr || transformMember == nullptr ||
                componentsMember == nullptr || componentsMember->type != JsonType::Array)
            {
                return cue::Result<cue::scene::SceneDocument>::failure(format_error(
                    a_assertContext, cue::scene::SceneError::InvalidFormat, "Scene object entry is invalid"));
            }
            auto objectId = cue::scene::ObjectId::parse(idMember->text, a_assertContext);
            std::optional<cue::scene::ObjectId> parentId;
            if (parentMember->type == JsonType::String)
            {
                auto parsedParent = cue::scene::ObjectId::parse(parentMember->text, a_assertContext);
                if (!parsedParent)
                {
                    return cue::Result<cue::scene::SceneDocument>::failure(std::move(*parsedParent.try_error()));
                }
                parentId = std::move(*parsedParent.try_value());
            }
            else if (parentMember->type != JsonType::Null)
            {
                return cue::Result<cue::scene::SceneDocument>::failure(
                    format_error(a_assertContext, cue::scene::SceneError::InvalidFormat,
                                 "Scene parentObjectId must be string or null"));
            }
            auto transform = parse_transform(*transformMember, a_assertContext);
            if (!objectId || !transform)
            {
                return cue::Result<cue::scene::SceneDocument>::failure(
                    format_error(a_assertContext, cue::scene::SceneError::InvalidFormat,
                                 "Scene object identity or transform is invalid"));
            }
            if (nameMember->text.empty())
            {
                return cue::Result<cue::scene::SceneDocument>::failure(
                    format_error(a_assertContext, cue::scene::SceneError::InvalidName,
                                 "Scene object name must not be empty"));
            }
            const auto stableObjectId = *objectId.try_value();
            if (objectIndex.contains(stableObjectId))
            {
                return cue::Result<cue::scene::SceneDocument>::failure(
                    format_error(a_assertContext,
                                 cue::scene::SceneError::DuplicateObjectId,
                                 "Scene object identity must be unique within a document"));
            }
            std::vector<cue::scene::SceneComponent> parsedComponents;
            parsedComponents.reserve(componentsMember->elements.size());
            for (const auto &component : componentsMember->elements)
            {
                auto parsedComponent = parse_component(a_json, component, a_schemaRegistry, a_valueSchemaRegistry,
                                                       a_componentMigrations, a_assertContext);
                if (!parsedComponent)
                {
                    return cue::Result<cue::scene::SceneDocument>::failure(std::move(*parsedComponent.try_error()));
                }
                std::string retainedComponent;
                if (!append_component(retainedComponent,
                                      *parsedComponent.try_value()))
                {
                    return cue::Result<cue::scene::SceneDocument>::failure(
                        format_error(a_assertContext, cue::scene::SceneError::MigrationFailed,
                                     "Migrated component exceeds the scene size limit"));
                }
                if (retainedComponent.size() > k_maximumSceneBytes - retainedComponentBytes)
                {
                    return cue::Result<cue::scene::SceneDocument>::failure(
                        format_error(a_assertContext, cue::scene::SceneError::MigrationFailed,
                                     "Migrated component data exceeds the scene size limit"));
                }
                retainedComponentBytes += retainedComponent.size();
                if (!componentIds
                         .emplace(parsedComponent.try_value()->instance_id())
                         .second)
                {
                    return cue::Result<cue::scene::SceneDocument>::failure(
                        format_error(a_assertContext,
                                     cue::scene::SceneError::DuplicateComponentId,
                                     "Component instance identity must be unique within a document"));
                }
                parsedComponents.push_back(
                    std::move(*parsedComponent.try_value()));
            }
            std::sort(
                parsedComponents.begin(), parsedComponents.end(),
                /// @brief Parse済みComponentをStable Instance Identity順へ並べる
                [](const cue::scene::SceneComponent &a_left,
                   const cue::scene::SceneComponent &a_right) noexcept
                {
                    return a_left.instance_id() < a_right.instance_id();
                });
            objectIndex.emplace(stableObjectId, parsedObjects.size());
            parsedObjects.push_back(
                cue::scene::SceneDocumentSerializationAccess::create_object(
                    stableObjectId, nameMember->text, activeMember->boolean,
                    std::move(parentId), std::move(*transform.try_value()),
                    std::move(parsedComponents)));
        }
        auto hierarchy = validate_scene_hierarchy(parsedObjects, objectIndex,
                                                  a_assertContext);
        if (!hierarchy)
        {
            return cue::Result<cue::scene::SceneDocument>::failure(
                std::move(*hierarchy.try_error()));
        }
        cue::scene::SceneDocumentSerializationAccess::set_objects(
            document, std::move(parsedObjects));
        std::string extensions(a_json.substr(extensionsMember->begin, extensionsMember->end - extensionsMember->begin));
        cue::scene::SceneDocumentSerializationAccess::set_extensions(document, std::move(extensions));
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
    auto validation = document.validate();
    if (!validation)
    {
        return cue::Result<cue::scene::SceneDocument>::failure(std::move(*validation.try_error()));
    }
    auto serialized = cue::scene::serialize_scene_document(document, a_assertContext);
    if (!serialized)
    {
        return cue::Result<cue::scene::SceneDocument>::failure(std::move(*serialized.try_error()));
    }
    return cue::Result<cue::scene::SceneDocument>::success(std::move(document));
}
} // namespace

namespace cue::scene
{
Result<SceneLoadResult> parse_scene_document(std::string_view a_json, const schema::SchemaRegistry &a_schemaRegistry,
                                             const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
                                             const SceneMigrationRegistry &a_migrationRegistry,
                                             const ComponentMigrationRegistry &a_componentMigrations,
                                             const AssertContext &a_assertContext) noexcept
{
    if (a_json.size() > k_maximumSceneBytes)
    {
        return Result<SceneLoadResult>::failure(format_error(
            a_assertContext, SceneError::ResourceLimitExceeded,
            "Scene file exceeds the 16 MiB input limit"));
    }
    if (a_json.empty() || a_json.starts_with("\xEF\xBB\xBF"))
    {
        return Result<SceneLoadResult>::failure(format_error(a_assertContext, SceneError::InvalidFormat,
                                                             "Scene must be non-empty BOM-less UTF-8 within 16 MiB"));
    }
    if (!a_valueSchemaRegistry.is_bound_to(a_schemaRegistry))
    {
        return Result<SceneLoadResult>::failure(
            format_error(a_assertContext, SceneError::InvalidComponentData,
                         "Component value schema belongs to a different schema registry generation"));
    }
    try
    {
        JsonValue originalRoot;
        std::string_view parseError;
        if (!cue::scene_private::parse_json_document(a_json, originalRoot, parseError))
        {
            return Result<SceneLoadResult>::failure(
                format_error(a_assertContext, SceneError::InvalidFormat, parseError));
        }
        std::uint32_t sourceVersion = 0U;
        if (!read_format_version(originalRoot, sourceVersion))
        {
            return Result<SceneLoadResult>::failure(
                format_error(a_assertContext, SceneError::InvalidFormat, "Scene formatVersion is missing or invalid"));
        }
        if (sourceVersion > k_currentSceneFormatVersion)
        {
            return Result<SceneLoadResult>::failure(format_error(a_assertContext, SceneError::UnsupportedFormatVersion,
                                                                 "Scene formatVersion is newer than this engine"));
        }
        std::string migratedStorage;
        std::string_view currentJson = a_json;
        if (sourceVersion < k_currentSceneFormatVersion)
        {
            auto migrated =
                a_migrationRegistry.migrate(a_json, sourceVersion, k_currentSceneFormatVersion, a_assertContext);
            if (!migrated)
            {
                return Result<SceneLoadResult>::failure(std::move(*migrated.try_error()));
            }
            migratedStorage = std::move(*migrated.try_value());
            currentJson = migratedStorage;
        }
        JsonValue currentRoot;
        if (!cue::scene_private::parse_json_document(currentJson, currentRoot, parseError))
        {
            return Result<SceneLoadResult>::failure(
                format_error(a_assertContext, SceneError::MigrationFailed, "Migrated scene is not valid JSON"));
        }
        std::uint32_t currentVersion = 0U;
        if (!read_format_version(currentRoot, currentVersion) || currentVersion != k_currentSceneFormatVersion)
        {
            return Result<SceneLoadResult>::failure(format_error(a_assertContext, SceneError::MigrationFailed,
                                                                 "Scene migration did not reach current format"));
        }
        auto document = parse_current_document(currentJson, currentRoot, a_schemaRegistry, a_valueSchemaRegistry,
                                               a_componentMigrations, a_assertContext);
        if (!document)
        {
            return Result<SceneLoadResult>::failure(std::move(*document.try_error()));
        }
        return Result<SceneLoadResult>::success(SceneLoadResult(std::move(*document.try_value()), sourceVersion));
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
}

Result<SceneLoadResult> load_scene_document(FilesystemRoot &a_filesystem, const RelativePath &a_path,
                                            const schema::SchemaRegistry &a_schemaRegistry,
                                            const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
                                            const SceneMigrationRegistry &a_migrationRegistry,
                                            const ComponentMigrationRegistry &a_componentMigrations,
                                            const AssertContext &a_assertContext) noexcept
{
    auto bytes = a_filesystem.read_file(a_path, k_maximumSceneBytes);
    if (!bytes)
    {
        return Result<SceneLoadResult>::failure(storage_error(a_assertContext, SceneError::StorageNotPublished,
                                                              "Failed to read scene file",
                                                              std::move(*bytes.try_error())));
    }
    const auto &storage = *bytes.try_value();
    return parse_scene_document(std::string_view(reinterpret_cast<const char *>(storage.data()), storage.size()),
                                a_schemaRegistry, a_valueSchemaRegistry, a_migrationRegistry, a_componentMigrations,
                                a_assertContext);
}

SceneSaveOutcome save_scene_document_internal(
    FilesystemRoot &a_filesystem, FileWriteLease *a_lease, const FileFingerprint *a_expected,
    const RelativePath &a_path, const SceneDocument &a_document, const schema::SchemaRegistry &a_schemaRegistry,
    const ComponentValueSchemaRegistry &a_valueSchemaRegistry, const SceneMigrationRegistry &a_migrationRegistry,
    const ComponentMigrationRegistry &a_componentMigrations, const AssertContext &a_assertContext) noexcept
{
    try
    {
        auto serialized = serialize_scene_document(a_document, a_assertContext);
        if (!serialized)
        {
            return SceneSaveOutcome::not_published(std::move(*serialized.try_error()));
        }
        auto parsedBack = parse_scene_document(*serialized.try_value(), a_schemaRegistry, a_valueSchemaRegistry,
                                               a_migrationRegistry, a_componentMigrations, a_assertContext);
        if (!parsedBack)
        {
            return SceneSaveOutcome::not_published(std::move(*parsedBack.try_error()));
        }
        auto reserialized = serialize_scene_document(parsedBack.try_value()->document(), a_assertContext);
        if (!reserialized || *reserialized.try_value() != *serialized.try_value())
        {
            return SceneSaveOutcome::not_published(format_error(a_assertContext, SceneError::ParseBackMismatch,
                                                                "Scene candidate differs after parse-back"));
        }

        auto entry = a_filesystem.query_entry(a_path);
        if (!entry)
        {
            return SceneSaveOutcome::not_published(storage_error(a_assertContext, SceneError::StorageNotPublished,
                                                                 "Failed to inspect scene destination",
                                                                 std::move(*entry.try_error())));
        }
        if (*entry.try_value() == EntryType::RegularFile)
        {
            auto original = a_filesystem.read_file(a_path, k_maximumSceneBytes);
            if (!original)
            {
                return SceneSaveOutcome::not_published(storage_error(a_assertContext, SceneError::StorageNotPublished,
                                                                     "Failed to read scene before backup",
                                                                     std::move(*original.try_error())));
            }
            std::string backupText(a_path.text());
            backupText.append(".backup");
            auto backupPath = RelativePath::parse(backupText, a_assertContext);
            if (!backupPath)
            {
                return SceneSaveOutcome::not_published(std::move(*backupPath.try_error()));
            }
            auto backupWritten = a_filesystem.write_file_atomic(*backupPath.try_value(), *original.try_value());
            if (!backupWritten)
            {
                return SceneSaveOutcome::not_published(storage_error(a_assertContext, SceneError::StorageNotPublished,
                                                                     "Failed to write scene recovery backup",
                                                                     std::move(*backupWritten.try_error())));
            }
        }
        else if (*entry.try_value() != EntryType::Missing)
        {
            return SceneSaveOutcome::not_published(format_error(a_assertContext, SceneError::StorageNotPublished,
                                                                "Scene destination is not a regular file"));
        }

        const auto characters = std::span(serialized.try_value()->data(), serialized.try_value()->size());
        auto written = a_expected == nullptr
                           ? a_filesystem.write_file_atomic(a_path, std::as_bytes(characters))
                           : a_filesystem.write_file_atomic_if_unchanged(
                                 *a_lease, a_path, *a_expected, k_maximumSceneBytes, std::as_bytes(characters));
        if (!written)
        {
            const bool durabilityUnknown =
                written.try_error()->root_code().domain() == "Cue.IO" &&
                written.try_error()->root_code().value() == static_cast<std::int64_t>(IoError::DurabilityUnknown);
            auto error = storage_error(a_assertContext,
                                       durabilityUnknown ? SceneError::StorageDurabilityUnknown
                                                         : SceneError::StorageNotPublished,
                                       durabilityUnknown ? "Scene is visible but storage durability is unknown"
                                                         : "Scene atomic publish failed before replacement",
                                       std::move(*written.try_error()));
            return durabilityUnknown ? SceneSaveOutcome::durability_unknown(std::move(error))
                                     : SceneSaveOutcome::not_published(std::move(error));
        }

        auto verified = load_scene_document(a_filesystem, a_path, a_schemaRegistry, a_valueSchemaRegistry,
                                            a_migrationRegistry, a_componentMigrations, a_assertContext);
        if (!verified)
        {
            return SceneSaveOutcome::verification_failed(storage_error(
                a_assertContext, SceneError::PublishedVerificationFailed,
                "Committed scene could not be read back for verification", std::move(*verified.try_error())));
        }
        auto verifiedText = serialize_scene_document(verified.try_value()->document(), a_assertContext);
        if (!verifiedText)
        {
            return SceneSaveOutcome::verification_failed(storage_error(
                a_assertContext, SceneError::PublishedVerificationFailed,
                "Committed scene could not be serialized for verification", std::move(*verifiedText.try_error())));
        }
        if (*verifiedText.try_value() != *serialized.try_value())
        {
            return SceneSaveOutcome::verification_failed(
                format_error(a_assertContext, SceneError::PublishedVerificationFailed,
                             "Committed scene differs during post-publish read-back verification"));
        }
        return SceneSaveOutcome::committed();
    }
    catch (...)
    {
        terminate_serialization_exception(a_assertContext);
    }
}

SceneSaveOutcome save_scene_document(FilesystemRoot &a_filesystem, const RelativePath &a_path,
                                     const SceneDocument &a_document, const schema::SchemaRegistry &a_schemaRegistry,
                                     const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
                                     const SceneMigrationRegistry &a_migrationRegistry,
                                     const ComponentMigrationRegistry &a_componentMigrations,
                                     const AssertContext &a_assertContext) noexcept
{
    return save_scene_document_internal(a_filesystem, nullptr, nullptr, a_path, a_document, a_schemaRegistry,
                                        a_valueSchemaRegistry, a_migrationRegistry, a_componentMigrations,
                                        a_assertContext);
}

SceneSaveOutcome save_scene_document_if_unchanged(
    FilesystemRoot &a_filesystem, FileWriteLease &a_lease, const RelativePath &a_path, FileFingerprint a_expected,
    const SceneDocument &a_document, const schema::SchemaRegistry &a_schemaRegistry,
    const ComponentValueSchemaRegistry &a_valueSchemaRegistry, const SceneMigrationRegistry &a_migrationRegistry,
    const ComponentMigrationRegistry &a_componentMigrations, const AssertContext &a_assertContext) noexcept
{
    return save_scene_document_internal(a_filesystem, &a_lease, &a_expected, a_path, a_document, a_schemaRegistry,
                                        a_valueSchemaRegistry, a_migrationRegistry, a_componentMigrations,
                                        a_assertContext);
}
} // namespace cue::scene
