#include <Cue/Scene/Identity.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Scene/Error.h>

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

/// @brief UUID内でHyphenを除く次のHexadecimal文字位置を返す
[[nodiscard]] std::size_t next_hex_position(std::size_t a_position) noexcept
{
    auto position = a_position;
    while (position == 8U || position == 13U || position == 18U ||
           position == 23U)
    {
        ++position;
    }
    return position;
}

/// @brief UUID Version 4とRFC 4122 Variantを持つnon-nil値か判定する
[[nodiscard]] bool is_valid_uuid_v4(const cue::scene::IdentityBytes &a_bytes) noexcept
{
    const bool isNil = std::all_of(
        a_bytes.begin(), a_bytes.end(),
        /// @brief UUID byteがnil値か判定する
        [](std::uint8_t a_value) noexcept
        {
            return a_value == 0U;
        });
    return !isNil && (a_bytes[6] & 0xF0U) == 0x40U &&
           (a_bytes[8] & 0xC0U) == 0x80U;
}

/// @brief canonical UUID文字列を検証して16 byteへ変換する
[[nodiscard]] cue::Result<cue::scene::IdentityBytes> parse_identity(
    std::string_view a_text, const cue::AssertContext &a_assertContext) noexcept
{
    if (a_text.size() != 36U || a_text[8] != '-' || a_text[13] != '-' ||
        a_text[18] != '-' || a_text[23] != '-')
    {
        return cue::Result<cue::scene::IdentityBytes>::failure(
            cue::scene::make_scene_error(
                a_assertContext, cue::scene::SceneError::InvalidIdentity,
                "Scene identity must use lowercase 8-4-4-4-12 UUID text"));
    }

    cue::scene::IdentityBytes bytes{};
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
            return cue::Result<cue::scene::IdentityBytes>::failure(
                cue::scene::make_scene_error(
                    a_assertContext, cue::scene::SceneError::InvalidIdentity,
                    "Scene identity contains a non-canonical hexadecimal character"));
        }
        byte = static_cast<std::uint8_t>((high << 4U) | low);
        textPosition += 2U;
    }

    if (!is_valid_uuid_v4(bytes))
    {
        return cue::Result<cue::scene::IdentityBytes>::failure(
            cue::scene::make_scene_error(
                a_assertContext, cue::scene::SceneError::InvalidIdentity,
                "Scene identity must be a non-nil RFC 4122 UUID Version 4"));
    }

    return cue::Result<cue::scene::IdentityBytes>::success(std::move(bytes));
}

/// @brief 16 byte UUIDをlowercase canonical固定長文字列へ変換する
[[nodiscard]] cue::scene::IdentityText format_identity(
    const cue::scene::IdentityBytes &a_bytes) noexcept
{
    constexpr char digits[] = "0123456789abcdef";
    cue::scene::IdentityText text{};
    std::size_t position = 0U;
    for (const auto byte : a_bytes)
    {
        if (position == 8U || position == 13U || position == 18U ||
            position == 23U)
        {
            text[position++] = '-';
        }
        text[position++] = digits[(byte >> 4U) & 0x0FU];
        text[position++] = digits[byte & 0x0FU];
    }
    return text;
}
} // namespace

namespace cue::scene
{
SceneAssetId::SceneAssetId(IdentityBytes a_bytes) noexcept : m_bytes(a_bytes)
{
}

SceneAssetId::~SceneAssetId() noexcept = default;

Result<SceneAssetId> SceneAssetId::generate(
    SceneIdentitySource &a_source,
    const AssertContext &a_assertContext) noexcept
{
    auto bytes = a_source.next_identity();
    if (!is_valid_uuid_v4(bytes))
    {
        return Result<SceneAssetId>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidIdentity,
            "SceneIdentitySource returned an invalid UUID Version 4"));
    }
    return Result<SceneAssetId>::success(SceneAssetId(std::move(bytes)));
}

Result<SceneAssetId> SceneAssetId::parse(
    std::string_view a_text,
    const AssertContext &a_assertContext) noexcept
{
    auto parsed = parse_identity(a_text, a_assertContext);
    if (!parsed)
    {
        return Result<SceneAssetId>::failure(std::move(*parsed.try_error()));
    }
    return Result<SceneAssetId>::success(
        SceneAssetId(std::move(*parsed.try_value())));
}

std::span<const std::uint8_t, 16> SceneAssetId::bytes() const noexcept
{
    return std::span<const std::uint8_t, 16>(m_bytes);
}

IdentityText SceneAssetId::canonical_text() const noexcept
{
    return format_identity(m_bytes);
}

ObjectId::ObjectId(IdentityBytes a_bytes) noexcept : m_bytes(a_bytes)
{
}

ObjectId::~ObjectId() noexcept = default;

Result<ObjectId> ObjectId::generate(
    SceneIdentitySource &a_source,
    const AssertContext &a_assertContext) noexcept
{
    auto bytes = a_source.next_identity();
    if (!is_valid_uuid_v4(bytes))
    {
        return Result<ObjectId>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidIdentity,
            "SceneIdentitySource returned an invalid UUID Version 4"));
    }
    return Result<ObjectId>::success(ObjectId(std::move(bytes)));
}

Result<ObjectId> ObjectId::parse(
    std::string_view a_text,
    const AssertContext &a_assertContext) noexcept
{
    auto parsed = parse_identity(a_text, a_assertContext);
    if (!parsed)
    {
        return Result<ObjectId>::failure(std::move(*parsed.try_error()));
    }
    return Result<ObjectId>::success(ObjectId(std::move(*parsed.try_value())));
}

std::span<const std::uint8_t, 16> ObjectId::bytes() const noexcept
{
    return std::span<const std::uint8_t, 16>(m_bytes);
}

IdentityText ObjectId::canonical_text() const noexcept
{
    return format_identity(m_bytes);
}

ComponentInstanceId::ComponentInstanceId(IdentityBytes a_bytes) noexcept
    : m_bytes(a_bytes)
{
}

ComponentInstanceId::~ComponentInstanceId() noexcept = default;

Result<ComponentInstanceId> ComponentInstanceId::generate(
    SceneIdentitySource &a_source,
    const AssertContext &a_assertContext) noexcept
{
    auto bytes = a_source.next_identity();
    if (!is_valid_uuid_v4(bytes))
    {
        return Result<ComponentInstanceId>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidIdentity,
            "SceneIdentitySource returned an invalid UUID Version 4"));
    }
    return Result<ComponentInstanceId>::success(
        ComponentInstanceId(std::move(bytes)));
}

Result<ComponentInstanceId> ComponentInstanceId::parse(
    std::string_view a_text,
    const AssertContext &a_assertContext) noexcept
{
    auto parsed = parse_identity(a_text, a_assertContext);
    if (!parsed)
    {
        return Result<ComponentInstanceId>::failure(
            std::move(*parsed.try_error()));
    }
    return Result<ComponentInstanceId>::success(
        ComponentInstanceId(std::move(*parsed.try_value())));
}

std::span<const std::uint8_t, 16> ComponentInstanceId::bytes() const noexcept
{
    return std::span<const std::uint8_t, 16>(m_bytes);
}

IdentityText ComponentInstanceId::canonical_text() const noexcept
{
    return format_identity(m_bytes);
}
} // namespace cue::scene
