#include <Cue/Schema/Types.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Schema/Error.h>

#include <algorithm>

namespace
{
/// @brief lowercase hexadecimal文字を4-bit値へ変換する
[[nodiscard]] bool decode_hex(char a_character, std::uint8_t &a_value) noexcept
{
    if (a_character >= '0' && a_character <= '9')
    {
        a_value = static_cast<std::uint8_t>(a_character - '0');
        return true;
    }

    if (a_character >= 'a' && a_character <= 'f')
    {
        a_value = static_cast<std::uint8_t>(a_character - 'a' + 10);
        return true;
    }

    return false;
}

/// @brief UUID内で固定Hyphenを除く次のHexadecimal文字位置を返す
[[nodiscard]] std::size_t next_hex_position(std::size_t a_position) noexcept
{
    auto position = a_position;

    while (position == 8U || position == 13U || position == 18U || position == 23U)
    {
        ++position;
    }

    return position;
}
} // namespace

namespace cue::schema
{
TypeId::TypeId(std::array<std::uint8_t, 16> a_bytes) noexcept : m_bytes(a_bytes)
{
}

Result<TypeId> TypeId::parse(std::string_view a_text,
                             const AssertContext &a_assertContext) noexcept
{
    if (a_text.size() != 36U || a_text[8] != '-' || a_text[13] != '-' ||
        a_text[18] != '-' || a_text[23] != '-')
    {
        return Result<TypeId>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidTypeId,
            "TypeId must use lowercase 8-4-4-4-12 UUID text"));
    }

    std::array<std::uint8_t, 16> bytes{};
    std::size_t textPosition = 0U;

    for (auto &byte : bytes)
    {
        textPosition = next_hex_position(textPosition);
        std::uint8_t high = 0U;
        std::uint8_t low = 0U;

        if (textPosition + 1U >= a_text.size() ||
            !decode_hex(a_text[textPosition], high) ||
            !decode_hex(a_text[textPosition + 1U], low))
        {
            return Result<TypeId>::failure(make_schema_error(
                a_assertContext, SchemaError::InvalidTypeId,
                "TypeId contains a non-canonical hexadecimal character"));
        }

        byte = static_cast<std::uint8_t>((high << 4U) | low);
        textPosition += 2U;
    }

    const bool isNil = std::all_of(bytes.begin(), bytes.end(),
                                   /// @brief UUIDの全Byteが0か判定する
                                   [](std::uint8_t a_value) noexcept
                                   {
                                       return a_value == 0U;
                                   });

    if (isNil || (bytes[6] & 0xF0U) != 0x40U || (bytes[8] & 0xC0U) != 0x80U)
    {
        return Result<TypeId>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidTypeId,
            "TypeId must be a non-nil RFC 4122 UUID Version 4"));
    }

    return Result<TypeId>::success(TypeId(bytes));
}

std::span<const std::uint8_t, 16> TypeId::bytes() const noexcept
{
    return std::span<const std::uint8_t, 16>(m_bytes);
}

FieldId::FieldId(std::uint32_t a_value) noexcept : m_value(a_value)
{
}

Result<FieldId> FieldId::create(std::uint32_t a_value,
                                const AssertContext &a_assertContext) noexcept
{
    if (a_value == 0U)
    {
        return Result<FieldId>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidFieldId,
            "FieldId zero is reserved as invalid"));
    }

    return Result<FieldId>::success(FieldId(a_value));
}

std::uint32_t FieldId::value() const noexcept
{
    return m_value;
}

SchemaVersion::SchemaVersion(std::uint32_t a_value) noexcept : m_value(a_value)
{
}

Result<SchemaVersion> SchemaVersion::create(
    std::uint32_t a_value, const AssertContext &a_assertContext) noexcept
{
    if (a_value == 0U)
    {
        return Result<SchemaVersion>::failure(make_schema_error(
            a_assertContext, SchemaError::InvalidSchemaVersion,
            "SchemaVersion zero is reserved as invalid"));
    }

    return Result<SchemaVersion>::success(SchemaVersion(a_value));
}

std::uint32_t SchemaVersion::value() const noexcept
{
    return m_value;
}

DenseTypeIndex::DenseTypeIndex(std::uint32_t a_value,
                               const SchemaRegistryIdentitySource &a_identitySource,
                               std::uint64_t a_registryGeneration) noexcept
    : m_value(a_value), m_identitySource(&a_identitySource),
      m_registryGeneration(a_registryGeneration)
{
}

std::uint32_t DenseTypeIndex::value() const noexcept
{
    return m_value;
}
} // namespace cue::schema
