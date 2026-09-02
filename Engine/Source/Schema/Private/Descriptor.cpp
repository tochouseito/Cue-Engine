#include <Cue/Schema/Descriptor.h>

#include "TypesInternal.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/Schema/Error.h>

#include <algorithm>
#include <new>

namespace
{
/// @brief UTF-8 継続 Byte か判定する
[[nodiscard]] bool is_continuation(std::uint8_t a_value) noexcept
{
    return (a_value & 0xC0U) == 0x80U;
}

/// @brief UTF-8 文字列が Scalar 列で Control 文字を含まないか検証する
[[nodiscard]] bool is_valid_diagnostic_name(std::string_view a_value,
                                            std::size_t a_maximumBytes) noexcept
{
    if (a_value.empty() || a_value.size() > a_maximumBytes)
    {
        return false;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(a_value.data());

    for (std::size_t index = 0U; index < a_value.size();)
    {
        const auto first = bytes[index];

        if (first <= 0x7FU)
        {
            if (first <= 0x1FU || first == 0x7FU)
            {
                return false;
            }

            ++index;
            continue;
        }

        if (first >= 0xC2U && first <= 0xDFU)
        {
            if (index + 1U >= a_value.size() || !is_continuation(bytes[index + 1U]))
            {
                return false;
            }

            const auto codePoint = static_cast<std::uint32_t>(first & 0x1FU) << 6U |
                                   static_cast<std::uint32_t>(bytes[index + 1U] & 0x3FU);

            if (codePoint >= 0x80U && codePoint <= 0x9FU)
            {
                return false;
            }

            index += 2U;
            continue;
        }

        if (first >= 0xE0U && first <= 0xEFU)
        {
            if (index + 2U >= a_value.size() || !is_continuation(bytes[index + 1U]) ||
                !is_continuation(bytes[index + 2U]))
            {
                return false;
            }

            if ((first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
                (first == 0xEDU && bytes[index + 1U] >= 0xA0U))
            {
                return false;
            }

            index += 3U;
            continue;
        }

        if (first >= 0xF0U && first <= 0xF4U)
        {
            if (index + 3U >= a_value.size() || !is_continuation(bytes[index + 1U]) ||
                !is_continuation(bytes[index + 2U]) ||
                !is_continuation(bytes[index + 3U]))
            {
                return false;
            }

            if ((first == 0xF0U && bytes[index + 1U] < 0x90U) ||
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

/// @brief Schema 所有値の Allocation 失敗を Emergency 終了へ変換する
[[noreturn]] void terminate_schema_allocation(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Schema allocation failed");
}

/// @brief Schema 構築中の予期しない例外を Emergency 終了へ変換する
[[noreturn]] void terminate_schema_exception(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Schema unexpected exception");
}

/// @brief TypeId を lowercase canonical UUID として既存診断文字列へ追記する
void append_type_id(std::string &a_destination, cue::schema::TypeId a_id)
{
    constexpr char hexDigits[] = "0123456789abcdef";
    std::size_t byteIndex = 0U;

    for (const auto byte : a_id.bytes())
    {
        if (byteIndex == 4U || byteIndex == 6U || byteIndex == 8U || byteIndex == 10U)
        {
            a_destination.push_back('-');
        }

        a_destination.push_back(hexDigits[(byte >> 4U) & 0x0FU]);
        a_destination.push_back(hexDigits[byte & 0x0FU]);
        ++byteIndex;
    }
}

/// @brief Field 衝突の Type と両方の Field 当事者を一つの Error へ保持する
[[nodiscard]] cue::Error make_field_collision_error(
    const cue::AssertContext &a_assertContext, cue::schema::SchemaError a_code,
    std::string_view a_rule, cue::schema::TypeId a_typeId,
    std::string_view a_typeName, std::uint32_t a_existingFieldId,
    std::string_view a_existingFieldName, std::uint32_t a_incomingFieldId,
    std::string_view a_incomingFieldName) noexcept
{
    try
    {
        std::string summary(a_rule);
        summary.append(" TypeId=");
        append_type_id(summary, a_typeId);
        summary.append(" TypeName=");
        summary.append(a_typeName);
        summary.append(" ExistingFieldId=");
        summary.append(std::to_string(a_existingFieldId));
        summary.append(" ExistingFieldName=");
        summary.append(a_existingFieldName);
        summary.append(" IncomingFieldId=");
        summary.append(std::to_string(a_incomingFieldId));
        summary.append(" IncomingFieldName=");
        summary.append(a_incomingFieldName);
        return cue::schema::make_schema_error(a_assertContext, a_code, summary);
    }
    catch (const std::bad_alloc &)
    {
        terminate_schema_allocation(a_assertContext);
    }
    catch (...)
    {
        terminate_schema_exception(a_assertContext);
    }
}
} // namespace

namespace cue::schema
{
FieldDescriptor::FieldDescriptor(FieldId a_id, std::string &&a_name) noexcept
    : m_id(a_id), m_name(std::move(a_name))
{
}

FieldDescriptor::FieldDescriptor(FieldDescriptor &&a_other) noexcept
    : m_id(a_other.m_id), m_name(std::move(a_other.m_name))
{
    a_other.m_name.clear();
}

FieldDescriptor &FieldDescriptor::operator=(FieldDescriptor &&a_other) noexcept
{
    if (this != &a_other)
    {
        m_id = a_other.m_id;
        m_name = std::move(a_other.m_name);
        a_other.m_name.clear();
    }

    return *this;
}

FieldId FieldDescriptor::id() const noexcept
{
    return m_id;
}

std::string_view FieldDescriptor::name() const noexcept
{
    return m_name;
}

TypeDescriptor::TypeDescriptor(TypeId a_id, std::string &&a_name,
                               SchemaVersion a_version,
                               std::vector<FieldDescriptor> &&a_fields,
                               std::vector<FieldId> &&a_reservedFieldIds) noexcept
    : m_id(a_id), m_name(std::move(a_name)), m_version(a_version),
      m_fields(std::move(a_fields)), m_reservedFieldIds(std::move(a_reservedFieldIds))
{
}

TypeDescriptor::TypeDescriptor(TypeDescriptor &&a_other) noexcept
    : m_id(a_other.m_id), m_name(std::move(a_other.m_name)),
      m_version(a_other.m_version), m_fields(std::move(a_other.m_fields)),
      m_reservedFieldIds(std::move(a_other.m_reservedFieldIds))
{
    a_other.m_name.clear();
}

TypeDescriptor &TypeDescriptor::operator=(TypeDescriptor &&a_other) noexcept
{
    if (this != &a_other)
    {
        m_id = a_other.m_id;
        m_name = std::move(a_other.m_name);
        m_version = a_other.m_version;
        m_fields = std::move(a_other.m_fields);
        m_reservedFieldIds = std::move(a_other.m_reservedFieldIds);
        a_other.m_name.clear();
    }

    return *this;
}

TypeId TypeDescriptor::id() const noexcept
{
    return m_id;
}

std::string_view TypeDescriptor::name() const noexcept
{
    return m_name;
}

SchemaVersion TypeDescriptor::version() const noexcept
{
    return m_version;
}

std::span<const FieldDescriptor> TypeDescriptor::fields() const noexcept
{
    return m_fields;
}

Result<const FieldDescriptor *> TypeDescriptor::find_field(
    FieldId a_id, const AssertContext &a_assertContext) const noexcept
{
    const auto iterator = std::lower_bound(
        m_fields.begin(), m_fields.end(), a_id,
        /// @brief Field Descriptor の Stable FieldId が検索値より小さいか判定する
        [](const FieldDescriptor &a_descriptor, FieldId a_value) noexcept
        {
            return a_descriptor.id() < a_value;
        });

    if (iterator != m_fields.end() && iterator->id() == a_id)
    {
        return Result<const FieldDescriptor *>::success(&*iterator);
    }

    return Result<const FieldDescriptor *>::failure(make_schema_error(
        a_assertContext, SchemaError::NotFound,
        "Schema FieldId is not registered in this type descriptor"));
}

std::span<const FieldId> TypeDescriptor::reserved_field_ids() const noexcept
{
    return m_reservedFieldIds;
}

Result<void> validate_type_descriptor(
    const TypeDescriptor &a_descriptor,
    const AssertContext &a_assertContext) noexcept
{
    if (!is_valid_type_id(a_descriptor.id()))
    {
        return Result<void>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidTypeId,
            "TypeId must be a non-nil RFC 4122 UUID Version 4"));
    }

    if (a_descriptor.version().value() == 0U)
    {
        return Result<void>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidSchemaVersion,
            "SchemaVersion zero is reserved as invalid"));
    }

    if (!is_valid_diagnostic_name(a_descriptor.name(), 255U))
    {
        return Result<void>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidName,
            "Type name must be valid UTF-8 without control characters and at most 255 bytes"));
    }

    for (const auto &field : a_descriptor.fields())
    {
        if (field.id().value() == 0U)
        {
            return Result<void>::failure(make_schema_error(
                a_assertContext, SchemaError::InvalidFieldId,
                "FieldId zero is reserved as invalid"));
        }

        if (!is_valid_diagnostic_name(field.name(), 128U))
        {
            return Result<void>::failure(make_schema_error(
                a_assertContext, SchemaError::InvalidName,
                "Field name must be valid UTF-8 without control characters and at most 128 bytes"));
        }
    }

    for (const auto reservedId : a_descriptor.reserved_field_ids())
    {
        if (reservedId.value() == 0U)
        {
            return Result<void>::failure(make_schema_error(
                a_assertContext, SchemaError::InvalidFieldId,
                "Reserved FieldId zero is invalid"));
        }
    }

    const auto fields = a_descriptor.fields();

    for (std::size_t index = 1U; index < fields.size(); ++index)
    {
        if (fields[index - 1U].id() >= fields[index].id())
        {
            const bool isDuplicate =
                fields[index - 1U].id() == fields[index].id();
            const auto code = isDuplicate ? SchemaError::DuplicateFieldId
                                          : SchemaError::InvalidFieldId;
            const auto rule = isDuplicate ? "DuplicateFieldId"
                                          : "InvalidFieldOrder";
            return Result<void>::failure(make_field_collision_error(
                a_assertContext, code, rule, a_descriptor.id(),
                a_descriptor.name(), fields[index - 1U].id().value(),
                fields[index - 1U].name(), fields[index].id().value(),
                fields[index].name()));
        }
    }

    for (std::size_t left = 0U; left < fields.size(); ++left)
    {
        for (std::size_t right = left + 1U; right < fields.size(); ++right)
        {
            if (fields[left].name() == fields[right].name())
            {
                return Result<void>::failure(make_field_collision_error(
                    a_assertContext, SchemaError::DuplicateFieldName,
                    "DuplicateFieldName", a_descriptor.id(), a_descriptor.name(),
                    fields[left].id().value(), fields[left].name(),
                    fields[right].id().value(), fields[right].name()));
            }
        }
    }

    const auto reservedIds = a_descriptor.reserved_field_ids();

    for (std::size_t index = 1U; index < reservedIds.size(); ++index)
    {
        if (reservedIds[index - 1U] >= reservedIds[index])
        {
            const auto rule = reservedIds[index - 1U] == reservedIds[index]
                                  ? "DuplicateReservedFieldId"
                                  : "InvalidReservedFieldOrder";
            return Result<void>::failure(make_field_collision_error(
                a_assertContext, SchemaError::ReservedFieldId, rule,
                a_descriptor.id(), a_descriptor.name(),
                reservedIds[index - 1U].value(), "<reserved>",
                reservedIds[index].value(), "<reserved>"));
        }
    }

    for (const auto &field : fields)
    {
        if (std::binary_search(reservedIds.begin(), reservedIds.end(), field.id()))
        {
            return Result<void>::failure(make_field_collision_error(
                a_assertContext, SchemaError::ReservedFieldId,
                "ActiveFieldIdReusesReservedFieldId", a_descriptor.id(),
                a_descriptor.name(), field.id().value(), "<reserved>",
                field.id().value(), field.name()));
        }
    }

    return Result<void>::success();
}

Result<FieldDescriptor> create_field_descriptor(
    FieldId a_id, std::string_view a_name, const AssertContext &a_assertContext) noexcept
{
    if (a_id.value() == 0U)
    {
        return Result<FieldDescriptor>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidFieldId,
            "FieldId zero is reserved as invalid"));
    }

    if (!is_valid_diagnostic_name(a_name, 128U))
    {
        return Result<FieldDescriptor>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidName,
            "Field name must be valid UTF-8 without control characters and at most 128 bytes"));
    }

    try
    {
        return Result<FieldDescriptor>::success(
            FieldDescriptor(a_id, std::string(a_name)));
    }
    catch (const std::bad_alloc &)
    {
        terminate_schema_allocation(a_assertContext);
    }
    catch (...)
    {
        terminate_schema_exception(a_assertContext);
    }
}

Result<TypeDescriptor> create_type_descriptor(
    TypeId a_id, std::string_view a_name, SchemaVersion a_version,
    std::vector<FieldDescriptor> &&a_fields, std::vector<FieldId> &&a_reservedFieldIds,
    const AssertContext &a_assertContext) noexcept
{
    if (!is_valid_type_id(a_id))
    {
        return Result<TypeDescriptor>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidTypeId,
            "TypeId must be a non-nil RFC 4122 UUID Version 4"));
    }

    if (a_version.value() == 0U)
    {
        return Result<TypeDescriptor>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidSchemaVersion,
            "SchemaVersion zero is reserved as invalid"));
    }

    if (!is_valid_diagnostic_name(a_name, 255U))
    {
        return Result<TypeDescriptor>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidName,
            "Type name must be valid UTF-8 without control characters and at most 255 bytes"));
    }

    std::string ownedName;

    try
    {
        ownedName.assign(a_name);
    }
    catch (const std::bad_alloc &)
    {
        terminate_schema_allocation(a_assertContext);
    }
    catch (...)
    {
        terminate_schema_exception(a_assertContext);
    }

    for (const auto &field : a_fields)
    {
        if (field.id().value() == 0U)
        {
            return Result<TypeDescriptor>::failure(make_schema_error(
                a_assertContext, SchemaError::InvalidFieldId,
                "FieldId zero is reserved as invalid"));
        }

        if (!is_valid_diagnostic_name(field.name(), 128U))
        {
            return Result<TypeDescriptor>::failure(make_schema_error(
                a_assertContext, SchemaError::InvalidName,
                "Field name must be valid UTF-8 without control characters and at most 128 bytes"));
        }
    }

    for (const auto reservedId : a_reservedFieldIds)
    {
        if (reservedId.value() == 0U)
        {
            return Result<TypeDescriptor>::failure(make_schema_error(
                a_assertContext, SchemaError::InvalidFieldId,
                "Reserved FieldId zero is invalid"));
        }
    }

    std::sort(a_fields.begin(), a_fields.end(),
              /// @brief Field Descriptor を Stable FieldId 順へ並べる
              [](const FieldDescriptor &a_left, const FieldDescriptor &a_right) noexcept
              {
                  return a_left.id() < a_right.id();
              });
    std::sort(a_reservedFieldIds.begin(), a_reservedFieldIds.end());

    for (std::size_t index = 1U; index < a_fields.size(); ++index)
    {
        if (a_fields[index - 1U].id() == a_fields[index].id())
        {
            return Result<TypeDescriptor>::failure(make_field_collision_error(
                a_assertContext, SchemaError::DuplicateFieldId, "DuplicateFieldId",
                a_id, ownedName, a_fields[index - 1U].id().value(),
                a_fields[index - 1U].name(), a_fields[index].id().value(),
                a_fields[index].name()));
        }
    }

    for (std::size_t left = 0U; left < a_fields.size(); ++left)
    {
        for (std::size_t right = left + 1U; right < a_fields.size(); ++right)
        {
            if (a_fields[left].name() == a_fields[right].name())
            {
                return Result<TypeDescriptor>::failure(make_field_collision_error(
                    a_assertContext, SchemaError::DuplicateFieldName,
                    "DuplicateFieldName", a_id, ownedName, a_fields[left].id().value(),
                    a_fields[left].name(), a_fields[right].id().value(),
                    a_fields[right].name()));
            }
        }
    }

    for (std::size_t index = 1U; index < a_reservedFieldIds.size(); ++index)
    {
        if (a_reservedFieldIds[index - 1U] == a_reservedFieldIds[index])
        {
            return Result<TypeDescriptor>::failure(make_field_collision_error(
                a_assertContext, SchemaError::ReservedFieldId,
                "DuplicateReservedFieldId", a_id, ownedName,
                a_reservedFieldIds[index - 1U].value(), "<reserved>",
                a_reservedFieldIds[index].value(), "<reserved>"));
        }
    }

    for (const auto &field : a_fields)
    {
        if (std::binary_search(a_reservedFieldIds.begin(), a_reservedFieldIds.end(),
                               field.id()))
        {
            return Result<TypeDescriptor>::failure(make_field_collision_error(
                a_assertContext, SchemaError::ReservedFieldId,
                "ActiveFieldIdReusesReservedFieldId", a_id, ownedName,
                field.id().value(), "<reserved>", field.id().value(), field.name()));
        }
    }

    try
    {
        TypeDescriptor descriptor(
            a_id, std::move(ownedName), a_version, std::move(a_fields),
            std::move(a_reservedFieldIds));
        auto validation = validate_type_descriptor(descriptor, a_assertContext);

        if (!validation)
        {
            return Result<TypeDescriptor>::failure(
                std::move(*validation.try_error()));
        }

        return Result<TypeDescriptor>::success(std::move(descriptor));
    }
    catch (const std::bad_alloc &)
    {
        terminate_schema_allocation(a_assertContext);
    }
    catch (...)
    {
        terminate_schema_exception(a_assertContext);
    }
}
} // namespace cue::schema
