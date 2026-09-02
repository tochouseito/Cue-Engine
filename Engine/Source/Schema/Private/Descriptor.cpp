#include <Cue/Schema/Descriptor.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Schema/Error.h>

#include <algorithm>
#include <new>

namespace
{
/// @brief UTF-8継続Byteか判定する
[[nodiscard]] bool is_continuation(std::uint8_t a_value) noexcept
{
    return (a_value & 0xC0U) == 0x80U;
}

/// @brief UTF-8文字列がScalar列でControl文字を含まないか検証する
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

/// @brief Schema所有値のAllocation失敗をEmergency終了へ変換する
[[noreturn]] void terminate_schema_allocation(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Schema allocation failed");
}

/// @brief Schema構築中の予期しない例外をEmergency終了へ変換する
[[noreturn]] void terminate_schema_exception(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Cue.Schema unexpected exception");
}
} // namespace

namespace cue::schema
{
FieldDescriptor::FieldDescriptor(FieldId a_id, std::string &&a_name) noexcept
    : m_id(a_id), m_name(std::move(a_name))
{
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

std::span<const FieldId> TypeDescriptor::reserved_field_ids() const noexcept
{
    return m_reservedFieldIds;
}

Result<FieldDescriptor> create_field_descriptor(
    FieldId a_id, std::string_view a_name, const AssertContext &a_assertContext) noexcept
{
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
    if (!is_valid_diagnostic_name(a_name, 255U))
    {
        return Result<TypeDescriptor>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidName,
            "Type name must be valid UTF-8 without control characters and at most 255 bytes"));
    }

    std::sort(a_fields.begin(), a_fields.end(),
              /// @brief Field DescriptorをStable FieldId順へ並べる
              [](const FieldDescriptor &a_left, const FieldDescriptor &a_right) noexcept
              {
                  return a_left.id() < a_right.id();
              });
    std::sort(a_reservedFieldIds.begin(), a_reservedFieldIds.end());

    for (std::size_t index = 1U; index < a_fields.size(); ++index)
    {
        if (a_fields[index - 1U].id() == a_fields[index].id())
        {
            return Result<TypeDescriptor>::failure(make_schema_error(
                a_assertContext, SchemaError::DuplicateFieldId,
                "Type descriptor contains a duplicate FieldId"));
        }
    }

    for (std::size_t left = 0U; left < a_fields.size(); ++left)
    {
        for (std::size_t right = left + 1U; right < a_fields.size(); ++right)
        {
            if (a_fields[left].name() == a_fields[right].name())
            {
                return Result<TypeDescriptor>::failure(make_schema_error(
                    a_assertContext, SchemaError::DuplicateFieldName,
                    "Type descriptor contains a duplicate field name"));
            }
        }
    }

    for (std::size_t index = 1U; index < a_reservedFieldIds.size(); ++index)
    {
        if (a_reservedFieldIds[index - 1U] == a_reservedFieldIds[index])
        {
            return Result<TypeDescriptor>::failure(make_schema_error(
                a_assertContext, SchemaError::ReservedFieldId,
                "Type descriptor contains a duplicate reserved FieldId"));
        }
    }

    for (const auto &field : a_fields)
    {
        if (std::binary_search(a_reservedFieldIds.begin(), a_reservedFieldIds.end(),
                               field.id()))
        {
            return Result<TypeDescriptor>::failure(make_schema_error(
                a_assertContext, SchemaError::ReservedFieldId,
                "Active FieldId cannot reuse a reserved FieldId"));
        }
    }

    try
    {
        return Result<TypeDescriptor>::success(TypeDescriptor(
            a_id, std::string(a_name), a_version, std::move(a_fields),
            std::move(a_reservedFieldIds)));
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
