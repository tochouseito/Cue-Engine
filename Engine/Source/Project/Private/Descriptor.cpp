#include <Cue/Project/Descriptor.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/IO/Filesystem.h>
#include <Cue/Project/Error.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t k_maximumDescriptorBytes = 1024U * 1024U;
constexpr std::size_t k_maximumStringBytes = 256U * 1024U;
constexpr std::size_t k_maximumContainerElements = 4096U;
constexpr std::size_t k_maximumNestingDepth = 32U;
constexpr std::uint32_t k_supportedSchemaVersion = 1U;

enum class JsonType : std::uint8_t
{
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

struct JsonValue final
{
    JsonType type = JsonType::Null;
    bool boolean = false;
    std::string text;
    std::vector<JsonValue> elements;
    std::vector<std::pair<std::string, JsonValue>> members;
};

/// @brief UTF-8 の先頭 Byte から Scalar と消費 Byte 数を厳密に復号する
[[nodiscard]] bool decode_utf8_scalar(std::string_view a_text, std::size_t a_offset, std::uint32_t &a_scalar,
                                      std::size_t &a_length) noexcept
{
    const auto first = static_cast<std::uint8_t>(a_text[a_offset]);
    if (first <= 0x7FU)
    {
        a_scalar = first;
        a_length = 1U;
        return true;
    }

    std::uint32_t minimum = 0;
    if (first >= 0xC2U && first <= 0xDFU)
    {
        a_scalar = first & 0x1FU;
        a_length = 2U;
        minimum = 0x80U;
    }
    else if (first >= 0xE0U && first <= 0xEFU)
    {
        a_scalar = first & 0x0FU;
        a_length = 3U;
        minimum = 0x800U;
    }
    else if (first >= 0xF0U && first <= 0xF4U)
    {
        a_scalar = first & 0x07U;
        a_length = 4U;
        minimum = 0x10000U;
    }
    else
    {
        return false;
    }

    if (a_offset + a_length > a_text.size())
    {
        return false;
    }
    for (std::size_t index = 1U; index < a_length; ++index)
    {
        const auto continuation = static_cast<std::uint8_t>(a_text[a_offset + index]);
        if ((continuation & 0xC0U) != 0x80U)
        {
            return false;
        }
        a_scalar = (a_scalar << 6U) | (continuation & 0x3FU);
    }

    return a_scalar >= minimum && a_scalar <= 0x10FFFFU && !(a_scalar >= 0xD800U && a_scalar <= 0xDFFFU);
}

/// @brief Unicode Scalar を canonical UTF-8 Byte 列として末尾へ追加する
void append_utf8_scalar(std::string &a_output, std::uint32_t a_scalar)
{
    if (a_scalar <= 0x7FU)
    {
        a_output.push_back(static_cast<char>(a_scalar));
    }
    else if (a_scalar <= 0x7FFU)
    {
        a_output.push_back(static_cast<char>(0xC0U | (a_scalar >> 6U)));
        a_output.push_back(static_cast<char>(0x80U | (a_scalar & 0x3FU)));
    }
    else if (a_scalar <= 0xFFFFU)
    {
        a_output.push_back(static_cast<char>(0xE0U | (a_scalar >> 12U)));
        a_output.push_back(static_cast<char>(0x80U | ((a_scalar >> 6U) & 0x3FU)));
        a_output.push_back(static_cast<char>(0x80U | (a_scalar & 0x3FU)));
    }
    else
    {
        a_output.push_back(static_cast<char>(0xF0U | (a_scalar >> 18U)));
        a_output.push_back(static_cast<char>(0x80U | ((a_scalar >> 12U) & 0x3FU)));
        a_output.push_back(static_cast<char>(0x80U | ((a_scalar >> 6U) & 0x3FU)));
        a_output.push_back(static_cast<char>(0x80U | (a_scalar & 0x3FU)));
    }
}

/// @brief JSON String の `\\u` に続く 4 桁 Hex を 16-bit 値へ変換する
[[nodiscard]] bool parse_hex_quad(std::string_view a_text, std::size_t a_offset, std::uint32_t &a_value) noexcept
{
    if (a_offset + 4U > a_text.size())
    {
        return false;
    }
    a_value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        const char character = a_text[a_offset + index];
        std::uint32_t digit = 0U;
        if (character >= '0' && character <= '9')
        {
            digit = static_cast<std::uint32_t>(character - '0');
        }
        else if (character >= 'a' && character <= 'f')
        {
            digit = static_cast<std::uint32_t>(character - 'a' + 10);
        }
        else if (character >= 'A' && character <= 'F')
        {
            digit = static_cast<std::uint32_t>(character - 'A' + 10);
        }
        else
        {
            return false;
        }
        a_value = (a_value << 4U) | digit;
    }
    return true;
}

class JsonParser final
{
  public:
    /// @brief 参照先 JSON Text を Parser の寿命中だけ保持する
    explicit JsonParser(std::string_view a_input) noexcept : m_input(a_input)
    {
    }

    /// @brief JSON 文書全体を解析し、末尾の非空白 Byte も拒否する
    [[nodiscard]] bool parse(JsonValue &a_value)
    {
        skip_whitespace();
        if (!parse_value(a_value, 1U))
        {
            return false;
        }
        skip_whitespace();
        if (m_offset != m_input.size())
        {
            return fail("JSON document has trailing data");
        }
        return true;
    }

    /// @brief 最初に検出した解析失敗の安定した Summary を返す
    [[nodiscard]] std::string_view error() const noexcept
    {
        return m_error;
    }

  private:
    /// @brief 現在位置の JSON Value を型に応じて解析する
    [[nodiscard]] bool parse_value(JsonValue &a_value, std::size_t a_depth)
    {
        if (m_offset >= m_input.size())
        {
            return fail("JSON value is missing");
        }
        const char current = m_input[m_offset];
        if (current == '{')
        {
            return parse_object(a_value, a_depth);
        }
        if (current == '[')
        {
            return parse_array(a_value, a_depth);
        }
        if (current == '"')
        {
            a_value.type = JsonType::String;
            return parse_string(a_value.text);
        }
        if (current == 't')
        {
            a_value.type = JsonType::Boolean;
            a_value.boolean = true;
            return consume_literal("true");
        }
        if (current == 'f')
        {
            a_value.type = JsonType::Boolean;
            a_value.boolean = false;
            return consume_literal("false");
        }
        if (current == 'n')
        {
            a_value.type = JsonType::Null;
            return consume_literal("null");
        }
        if (current == '-' || (current >= '0' && current <= '9'))
        {
            a_value.type = JsonType::Number;
            return parse_number(a_value.text);
        }
        return fail("JSON value starts with an invalid token");
    }

    /// @brief JSON Object の Member、重複名、要素上限を検証する
    [[nodiscard]] bool parse_object(JsonValue &a_value, std::size_t a_depth)
    {
        if (a_depth > k_maximumNestingDepth)
        {
            return fail("JSON nesting exceeds 32 levels");
        }
        a_value.type = JsonType::Object;
        ++m_offset;
        skip_whitespace();
        if (consume_character('}'))
        {
            return true;
        }
        while (true)
        {
            if (a_value.members.size() >= k_maximumContainerElements)
            {
                return fail("JSON object exceeds 4096 members");
            }
            std::string name;
            if (!parse_string(name))
            {
                return false;
            }
            const bool duplicate = std::ranges::any_of(a_value.members, [&name](const auto &a_member) noexcept
                                                       { return a_member.first == name; });
            if (duplicate)
            {
                return fail("JSON object contains a duplicate member");
            }
            skip_whitespace();
            if (!consume_character(':'))
            {
                return fail("JSON object member is missing ':'");
            }
            skip_whitespace();
            JsonValue value;
            if (!parse_value(value, a_depth + 1U))
            {
                return false;
            }
            a_value.members.emplace_back(std::move(name), std::move(value));
            skip_whitespace();
            if (consume_character('}'))
            {
                return true;
            }
            if (!consume_character(','))
            {
                return fail("JSON object member is missing ','");
            }
            skip_whitespace();
        }
    }

    /// @brief JSON Array の要素と要素上限を検証する
    [[nodiscard]] bool parse_array(JsonValue &a_value, std::size_t a_depth)
    {
        if (a_depth > k_maximumNestingDepth)
        {
            return fail("JSON nesting exceeds 32 levels");
        }
        a_value.type = JsonType::Array;
        ++m_offset;
        skip_whitespace();
        if (consume_character(']'))
        {
            return true;
        }
        while (true)
        {
            if (a_value.elements.size() >= k_maximumContainerElements)
            {
                return fail("JSON array exceeds 4096 elements");
            }
            JsonValue value;
            if (!parse_value(value, a_depth + 1U))
            {
                return false;
            }
            a_value.elements.push_back(std::move(value));
            skip_whitespace();
            if (consume_character(']'))
            {
                return true;
            }
            if (!consume_character(','))
            {
                return fail("JSON array element is missing ','");
            }
            skip_whitespace();
        }
    }

    /// @brief JSON Escape と UTF-8 を検証し、復号後の UTF-8 String を返す
    [[nodiscard]] bool parse_string(std::string &a_output)
    {
        if (!consume_character('"'))
        {
            return fail("JSON string is missing opening quote");
        }
        while (m_offset < m_input.size())
        {
            const auto current = static_cast<std::uint8_t>(m_input[m_offset]);
            if (current == static_cast<std::uint8_t>('"'))
            {
                ++m_offset;
                return true;
            }
            if (current == static_cast<std::uint8_t>('\\'))
            {
                if (!parse_escape(a_output))
                {
                    return false;
                }
            }
            else
            {
                if (current < 0x20U)
                {
                    return fail("JSON string contains an unescaped control byte");
                }
                std::uint32_t scalar = 0U;
                std::size_t length = 0U;
                if (!decode_utf8_scalar(m_input, m_offset, scalar, length))
                {
                    return fail("JSON string contains invalid UTF-8");
                }
                a_output.append(m_input.substr(m_offset, length));
                m_offset += length;
            }
            if (a_output.size() > k_maximumStringBytes)
            {
                return fail("JSON string exceeds 256 KiB after decoding");
            }
        }
        return fail("JSON string is missing closing quote");
    }

    /// @brief 現在位置の JSON Escape を一つの Unicode Scalar として追加する
    [[nodiscard]] bool parse_escape(std::string &a_output)
    {
        ++m_offset;
        if (m_offset >= m_input.size())
        {
            return fail("JSON escape is incomplete");
        }
        const char escaped = m_input[m_offset++];
        switch (escaped)
        {
        case '"':
        case '\\':
        case '/':
            a_output.push_back(escaped);
            return true;
        case 'b':
            a_output.push_back('\b');
            return true;
        case 'f':
            a_output.push_back('\f');
            return true;
        case 'n':
            a_output.push_back('\n');
            return true;
        case 'r':
            a_output.push_back('\r');
            return true;
        case 't':
            a_output.push_back('\t');
            return true;
        case 'u':
            break;
        default:
            return fail("JSON string contains an invalid escape");
        }

        std::uint32_t first = 0U;
        if (!parse_hex_quad(m_input, m_offset, first))
        {
            return fail("JSON unicode escape is invalid");
        }
        m_offset += 4U;
        std::uint32_t scalar = first;
        if (first >= 0xD800U && first <= 0xDBFFU)
        {
            if (m_offset + 6U > m_input.size() || m_input[m_offset] != '\\' || m_input[m_offset + 1U] != 'u')
            {
                return fail("JSON high surrogate is missing a low surrogate");
            }
            std::uint32_t second = 0U;
            if (!parse_hex_quad(m_input, m_offset + 2U, second) || second < 0xDC00U || second > 0xDFFFU)
            {
                return fail("JSON surrogate pair is invalid");
            }
            m_offset += 6U;
            scalar = 0x10000U + ((first - 0xD800U) << 10U) + (second - 0xDC00U);
        }
        else if (first >= 0xDC00U && first <= 0xDFFFU)
        {
            return fail("JSON low surrogate has no high surrogate");
        }
        append_utf8_scalar(a_output, scalar);
        return true;
    }

    /// @brief RFC 8259 の JSON Number Token を構文検証して原文のまま保持する
    [[nodiscard]] bool parse_number(std::string &a_output)
    {
        const std::size_t start = m_offset;
        const bool hasSign = consume_character('-');
        (void)hasSign;
        if (consume_character('0'))
        {
            if (m_offset < m_input.size() && m_input[m_offset] >= '0' && m_input[m_offset] <= '9')
            {
                return fail("JSON number has a leading zero");
            }
        }
        else if (!consume_digits())
        {
            return fail("JSON number has no integer digits");
        }
        if (consume_character('.'))
        {
            if (!consume_digits())
            {
                return fail("JSON number has no fraction digits");
            }
        }
        if (m_offset < m_input.size() && (m_input[m_offset] == 'e' || m_input[m_offset] == 'E'))
        {
            ++m_offset;
            if (m_offset < m_input.size() && (m_input[m_offset] == '+' || m_input[m_offset] == '-'))
            {
                ++m_offset;
            }
            if (!consume_digits())
            {
                return fail("JSON number has no exponent digits");
            }
        }
        a_output.assign(m_input.substr(start, m_offset - start));
        return true;
    }

    /// @brief 一つ以上の ASCII Decimal Digit を消費する
    [[nodiscard]] bool consume_digits() noexcept
    {
        const std::size_t start = m_offset;
        while (m_offset < m_input.size() && m_input[m_offset] >= '0' && m_input[m_offset] <= '9')
        {
            ++m_offset;
        }
        return m_offset != start;
    }

    /// @brief 期待する固定 JSON Literal を現在位置から消費する
    [[nodiscard]] bool consume_literal(std::string_view a_literal) noexcept
    {
        if (m_input.substr(m_offset, a_literal.size()) != a_literal)
        {
            return fail("JSON literal is invalid");
        }
        m_offset += a_literal.size();
        return true;
    }

    /// @brief 現在位置が期待 Character の場合だけ一文字消費する
    [[nodiscard]] bool consume_character(char a_character) noexcept
    {
        if (m_offset < m_input.size() && m_input[m_offset] == a_character)
        {
            ++m_offset;
            return true;
        }
        return false;
    }

    /// @brief JSON で許可される 4 種の ASCII Whitespace を読み飛ばす
    void skip_whitespace() noexcept
    {
        while (m_offset < m_input.size())
        {
            const char current = m_input[m_offset];
            if (current != ' ' && current != '\t' && current != '\r' && current != '\n')
            {
                return;
            }
            ++m_offset;
        }
    }

    /// @brief 最初の解析 Error だけを保持して false を返す
    [[nodiscard]] bool fail(std::string_view a_error) noexcept
    {
        if (m_error.empty())
        {
            m_error = a_error;
        }
        return false;
    }

    std::string_view m_input;
    std::size_t m_offset = 0U;
    std::string_view m_error;
};

/// @brief Object から指定名の Member を非所有で検索する
[[nodiscard]] const JsonValue *find_member(const JsonValue &a_object, std::string_view a_name) noexcept
{
    const auto iterator = std::ranges::find_if(a_object.members, [a_name](const auto &a_member) noexcept
                                               { return a_member.first == a_name; });
    return iterator == a_object.members.end() ? nullptr : &iterator->second;
}

/// @brief Object が指定された Member 名だけを一度ずつ持つか検証する
template <std::size_t Size>
[[nodiscard]] bool has_exact_members(const JsonValue &a_object,
                                     const std::array<std::string_view, Size> &a_names) noexcept
{
    if (a_object.type != JsonType::Object || a_object.members.size() != a_names.size())
    {
        return false;
    }
    return std::ranges::all_of(a_names, [&a_object](std::string_view a_name) noexcept
                               { return find_member(a_object, a_name) != nullptr; });
}

/// @brief canonical unsigned 32-bit Decimal を上限検査して数値化する
[[nodiscard]] bool parse_canonical_u32(std::string_view a_text, bool a_allowZero, std::uint32_t &a_value) noexcept
{
    if (a_text.empty() || (a_text.size() > 1U && a_text.front() == '0'))
    {
        return false;
    }
    const auto conversion = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_value);
    return conversion.ec == std::errc{} && conversion.ptr == a_text.data() + a_text.size() &&
           (a_allowZero || a_value != 0U);
}

/// @brief canonical major.minor.patch 文字列を EngineVersion へ変換する
[[nodiscard]] bool parse_engine_version(std::string_view a_text, cue::EngineVersion &a_version) noexcept
{
    const std::size_t first = a_text.find('.');
    const std::size_t second = first == std::string_view::npos ? first : a_text.find('.', first + 1U);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        a_text.find('.', second + 1U) != std::string_view::npos)
    {
        return false;
    }
    return parse_canonical_u32(a_text.substr(0U, first), true, a_version.major) &&
           parse_canonical_u32(a_text.substr(first + 1U, second - first - 1U), true, a_version.minor) &&
           parse_canonical_u32(a_text.substr(second + 1U), true, a_version.patch);
}

/// @brief Unicode Scalar Sequence が UI 名として許可される UTF-8 と非 Control 文字だけか検証する
[[nodiscard]] bool is_valid_display_name(std::string_view a_text) noexcept
{
    if (a_text.empty() || a_text.size() > 256U)
    {
        return false;
    }
    std::size_t offset = 0U;
    while (offset < a_text.size())
    {
        std::uint32_t scalar = 0U;
        std::size_t length = 0U;
        if (!decode_utf8_scalar(a_text, offset, scalar, length) || scalar <= 0x1FU ||
            (scalar >= 0x7FU && scalar <= 0x9FU))
        {
            return false;
        }
        offset += length;
    }
    return true;
}

/// @brief JSON String として必要な Character だけ Escape して追加する
void append_json_string(std::string &a_output, std::string_view a_text)
{
    constexpr char hexadecimal[] = "0123456789abcdef";
    a_output.push_back('"');
    for (const unsigned char character : a_text)
    {
        switch (character)
        {
        case '"':
            a_output.append("\\\"");
            break;
        case '\\':
            a_output.append("\\\\");
            break;
        case '\b':
            a_output.append("\\b");
            break;
        case '\f':
            a_output.append("\\f");
            break;
        case '\n':
            a_output.append("\\n");
            break;
        case '\r':
            a_output.append("\\r");
            break;
        case '\t':
            a_output.append("\\t");
            break;
        default:
            if (character < 0x20U)
            {
                a_output.append("\\u00");
                a_output.push_back(hexadecimal[character >> 4U]);
                a_output.push_back(hexadecimal[character & 0x0FU]);
            }
            else
            {
                a_output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    a_output.push_back('"');
}

/// @brief JSON DOM を意味変更しない compact JSON として直列化する
void append_json_value(std::string &a_output, const JsonValue &a_value)
{
    switch (a_value.type)
    {
    case JsonType::Null:
        a_output.append("null");
        break;
    case JsonType::Boolean:
        a_output.append(a_value.boolean ? "true" : "false");
        break;
    case JsonType::Number:
        a_output.append(a_value.text);
        break;
    case JsonType::String:
        append_json_string(a_output, a_value.text);
        break;
    case JsonType::Array:
        a_output.push_back('[');
        for (std::size_t index = 0U; index < a_value.elements.size(); ++index)
        {
            if (index != 0U)
            {
                a_output.push_back(',');
            }
            append_json_value(a_output, a_value.elements[index]);
        }
        a_output.push_back(']');
        break;
    case JsonType::Object:
        a_output.push_back('{');
        for (std::size_t index = 0U; index < a_value.members.size(); ++index)
        {
            if (index != 0U)
            {
                a_output.push_back(',');
            }
            append_json_string(a_output, a_value.members[index].first);
            a_output.push_back(':');
            append_json_value(a_output, a_value.members[index].second);
        }
        a_output.push_back('}');
        break;
    }
}

/// @brief 4 種 Root が Portable Comparison 上で重複も親子関係も持たないか検証する
[[nodiscard]] bool validate_roots(const cue::ProjectRoots &a_roots, const cue::AssertContext &a_assertContext) noexcept
{
    const std::array<std::string, 4U> keys = {
        a_roots.sourceAssets.comparison_key(a_assertContext), a_roots.runtimeAssets.comparison_key(a_assertContext),
        a_roots.generated.comparison_key(a_assertContext), a_roots.saved.comparison_key(a_assertContext)};
    for (const std::string &key : keys)
    {
        const std::size_t separator = key.find('/');
        if (key.substr(0U, separator) == "cueproject.json")
        {
            return false;
        }
    }
    for (std::size_t left = 0U; left < keys.size(); ++left)
    {
        for (std::size_t right = left + 1U; right < keys.size(); ++right)
        {
            const std::string leftPrefix = keys[left] + '/';
            const std::string rightPrefix = keys[right] + '/';
            if (keys[left] == keys[right] || keys[left].starts_with(rightPrefix) || keys[right].starts_with(leftPrefix))
            {
                return false;
            }
        }
    }
    return true;
}

/// @brief EngineVersion を canonical major.minor.patch 文字列として追加する
void append_engine_version(std::string &a_output, const cue::EngineVersion &a_version)
{
    a_output.append(std::to_string(a_version.major));
    a_output.push_back('.');
    a_output.append(std::to_string(a_version.minor));
    a_output.push_back('.');
    a_output.append(std::to_string(a_version.patch));
}

/// @brief IO Error を Project 読書き境界の Error へ Cause 付きで再分類する
[[nodiscard]] cue::Error reclassify_io_error(const cue::AssertContext &a_assertContext, std::string_view a_summary,
                                             cue::Error &&a_cause) noexcept
{
    cue::ErrorCode code = cue::ErrorCode::create(a_assertContext.fatal_handler(), "Cue.Project",
                                                 static_cast<std::int64_t>(cue::ProjectError::IoFailure));
    return cue::Error::reclassify(a_assertContext.fatal_handler(), std::move(code), a_summary, std::move(a_cause));
}
} // namespace

namespace cue
{
ProjectId::ProjectId(std::string &&a_text) noexcept : m_text(std::move(a_text))
{
}

Result<ProjectId> ProjectId::parse(std::string_view a_text, const AssertContext &a_assertContext) noexcept
{
    if (a_text.size() != 36U || a_text[8] != '-' || a_text[13] != '-' || a_text[18] != '-' || a_text[23] != '-' ||
        a_text[14] != '4' || (a_text[19] != '8' && a_text[19] != '9' && a_text[19] != 'a' && a_text[19] != 'b'))
    {
        return Result<ProjectId>::failure(make_project_error(a_assertContext, ProjectError::InvalidProjectId,
                                                             "ProjectId is not a canonical UUID version 4"));
    }
    for (std::size_t index = 0U; index < a_text.size(); ++index)
    {
        if (index == 8U || index == 13U || index == 18U || index == 23U)
        {
            continue;
        }
        const char character = a_text[index];
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
        {
            return Result<ProjectId>::failure(
                make_project_error(a_assertContext, ProjectError::InvalidProjectId,
                                   "ProjectId contains a non-lowercase hexadecimal digit"));
        }
    }
    return Result<ProjectId>::success(ProjectId(std::string(a_text)));
}

std::string_view ProjectId::text() const noexcept
{
    return m_text;
}

ProjectDescriptor::ProjectDescriptor(ProjectId &&a_projectId, std::string &&a_displayName,
                                     EngineCompatibility a_engineCompatibility, ProjectRoots &&a_roots,
                                     std::string &&a_extensionsJson) noexcept
    : m_projectId(std::move(a_projectId)), m_displayName(std::move(a_displayName)),
      m_engineCompatibility(a_engineCompatibility), m_roots(std::move(a_roots)),
      m_extensionsJson(std::move(a_extensionsJson))
{
}

std::uint32_t ProjectDescriptor::schema_version() const noexcept
{
    return k_supportedSchemaVersion;
}

const ProjectId &ProjectDescriptor::project_id() const noexcept
{
    return m_projectId;
}

std::string_view ProjectDescriptor::display_name() const noexcept
{
    return m_displayName;
}

const EngineCompatibility &ProjectDescriptor::engine_compatibility() const noexcept
{
    return m_engineCompatibility;
}

const ProjectRoots &ProjectDescriptor::roots() const noexcept
{
    return m_roots;
}

std::string_view ProjectDescriptor::extensions_json() const noexcept
{
    return m_extensionsJson;
}

bool ProjectDescriptor::equivalent_to(const ProjectDescriptor &a_other) const noexcept
{
    return m_projectId == a_other.m_projectId && m_displayName == a_other.m_displayName &&
           m_engineCompatibility == a_other.m_engineCompatibility &&
           m_roots.sourceAssets.text() == a_other.m_roots.sourceAssets.text() &&
           m_roots.runtimeAssets.text() == a_other.m_roots.runtimeAssets.text() &&
           m_roots.generated.text() == a_other.m_roots.generated.text() &&
           m_roots.saved.text() == a_other.m_roots.saved.text() && m_extensionsJson == a_other.m_extensionsJson;
}

Result<ProjectDescriptor> parse_project_descriptor(std::string_view a_json,
                                                   const AssertContext &a_assertContext) noexcept
{
    if (a_json.size() > k_maximumDescriptorBytes)
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidFormat, "Project descriptor exceeds 1 MiB"));
    }
    if (a_json.starts_with("\xEF\xBB\xBF"))
    {
        return Result<ProjectDescriptor>::failure(make_project_error(
            a_assertContext, ProjectError::InvalidFormat, "Project descriptor must not contain a UTF-8 BOM"));
    }

    JsonValue root;
    JsonParser parser(a_json);
    if (!parser.parse(root))
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidFormat, parser.error()));
    }
    constexpr std::array topLevelNames = {
        std::string_view("schemaVersion"),        std::string_view("projectId"), std::string_view("displayName"),
        std::string_view("engineCompatibility"),  std::string_view("roots"),     std::string_view("defaultScene"),
        std::string_view("requiredCapabilities"), std::string_view("extensions")};
    if (!has_exact_members(root, topLevelNames))
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidFormat,
                               "Project descriptor has missing or unknown top-level members"));
    }

    const JsonValue &schemaVersion = *find_member(root, "schemaVersion");
    std::uint32_t parsedSchemaVersion = 0U;
    if (schemaVersion.type != JsonType::Number || !parse_canonical_u32(schemaVersion.text, false, parsedSchemaVersion))
    {
        return Result<ProjectDescriptor>::failure(make_project_error(
            a_assertContext, ProjectError::InvalidFormat, "schemaVersion is not a canonical positive uint32"));
    }
    if (parsedSchemaVersion != k_supportedSchemaVersion)
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::UnsupportedSchemaVersion,
                               "Project descriptor schemaVersion is unsupported"));
    }

    const JsonValue &projectIdValue = *find_member(root, "projectId");
    if (projectIdValue.type != JsonType::String)
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidProjectId, "projectId must be a JSON string"));
    }
    Result<ProjectId> projectIdResult = ProjectId::parse(projectIdValue.text, a_assertContext);
    if (!projectIdResult)
    {
        return Result<ProjectDescriptor>::failure(std::move(*projectIdResult.try_error()));
    }

    const JsonValue &displayName = *find_member(root, "displayName");
    if (displayName.type != JsonType::String || !is_valid_display_name(displayName.text))
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidDisplayName,
                               "displayName is empty, too long, invalid UTF-8, or contains a control character"));
    }

    const JsonValue &compatibility = *find_member(root, "engineCompatibility");
    constexpr std::array compatibilityNames = {std::string_view("minimum"), std::string_view("maximumExclusive")};
    if (!has_exact_members(compatibility, compatibilityNames))
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidEngineCompatibility,
                               "engineCompatibility has missing or unknown members"));
    }
    const JsonValue &minimumValue = *find_member(compatibility, "minimum");
    EngineCompatibility engineCompatibility;
    if (minimumValue.type != JsonType::String || !parse_engine_version(minimumValue.text, engineCompatibility.minimum))
    {
        return Result<ProjectDescriptor>::failure(make_project_error(
            a_assertContext, ProjectError::InvalidEngineCompatibility, "minimum engine version is not canonical"));
    }
    const JsonValue &maximumValue = *find_member(compatibility, "maximumExclusive");
    if (maximumValue.type == JsonType::String)
    {
        EngineVersion maximum;
        if (!parse_engine_version(maximumValue.text, maximum) || maximum <= engineCompatibility.minimum)
        {
            return Result<ProjectDescriptor>::failure(
                make_project_error(a_assertContext, ProjectError::InvalidEngineCompatibility,
                                   "maximumExclusive must be canonical and greater than minimum"));
        }
        engineCompatibility.maximumExclusive = maximum;
    }
    else if (maximumValue.type != JsonType::Null)
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidEngineCompatibility,
                               "maximumExclusive must be a version string or null"));
    }

    const JsonValue &rootsValue = *find_member(root, "roots");
    constexpr std::array rootNames = {std::string_view("sourceAssets"), std::string_view("runtimeAssets"),
                                      std::string_view("generated"), std::string_view("saved")};
    if (!has_exact_members(rootsValue, rootNames))
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidRoots, "roots has missing or unknown members"));
    }
    const std::array<const JsonValue *, 4U> rootValues = {
        find_member(rootsValue, "sourceAssets"), find_member(rootsValue, "runtimeAssets"),
        find_member(rootsValue, "generated"), find_member(rootsValue, "saved")};
    if (!std::ranges::all_of(rootValues,
                             [](const JsonValue *a_value) noexcept { return a_value->type == JsonType::String; }))
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidRoots, "root role must be a JSON string"));
    }
    std::array<Result<RelativePath>, 4U> rootResults = {RelativePath::parse(rootValues[0]->text, a_assertContext),
                                                        RelativePath::parse(rootValues[1]->text, a_assertContext),
                                                        RelativePath::parse(rootValues[2]->text, a_assertContext),
                                                        RelativePath::parse(rootValues[3]->text, a_assertContext)};
    if (!std::ranges::all_of(rootResults,
                             [](const Result<RelativePath> &a_result) noexcept { return a_result.has_value(); }))
    {
        return Result<ProjectDescriptor>::failure(make_project_error(
            a_assertContext, ProjectError::InvalidRoots, "root role is not a valid portable relative path"));
    }
    ProjectRoots roots{std::move(*rootResults[0].try_value()), std::move(*rootResults[1].try_value()),
                       std::move(*rootResults[2].try_value()), std::move(*rootResults[3].try_value())};
    if (!validate_roots(roots, a_assertContext))
    {
        return Result<ProjectDescriptor>::failure(make_project_error(
            a_assertContext, ProjectError::InvalidRoots, "root roles overlap, nest, or collide with CueProject.json"));
    }

    const JsonValue &defaultScene = *find_member(root, "defaultScene");
    const JsonValue &requiredCapabilities = *find_member(root, "requiredCapabilities");
    if (defaultScene.type != JsonType::Null || requiredCapabilities.type != JsonType::Array ||
        !requiredCapabilities.elements.empty())
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidFormat,
                               "schema version 1 requires null defaultScene and an empty requiredCapabilities array"));
    }
    const JsonValue &extensions = *find_member(root, "extensions");
    if (extensions.type != JsonType::Object)
    {
        return Result<ProjectDescriptor>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidFormat, "extensions must be a JSON object"));
    }
    std::string extensionsJson;
    append_json_value(extensionsJson, extensions);

    return Result<ProjectDescriptor>::success(ProjectDescriptor(std::move(*projectIdResult.try_value()),
                                                                std::string(displayName.text), engineCompatibility,
                                                                std::move(roots), std::move(extensionsJson)));
}

Result<void> validate_project_descriptor(const ProjectDescriptor &a_descriptor,
                                         const AssertContext &a_assertContext) noexcept
{
    if (!is_valid_display_name(a_descriptor.display_name()))
    {
        return Result<void>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidDisplayName, "Descriptor displayName is invalid"));
    }
    if (a_descriptor.engine_compatibility().maximumExclusive.has_value() &&
        *a_descriptor.engine_compatibility().maximumExclusive <= a_descriptor.engine_compatibility().minimum)
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::InvalidEngineCompatibility,
                                                        "Descriptor maximumExclusive is not greater than minimum"));
    }
    if (!validate_roots(a_descriptor.roots(), a_assertContext))
    {
        return Result<void>::failure(
            make_project_error(a_assertContext, ProjectError::InvalidRoots, "Descriptor root roles overlap or nest"));
    }

    JsonValue extensions;
    JsonParser parser(a_descriptor.extensions_json());
    if (!parser.parse(extensions) || extensions.type != JsonType::Object)
    {
        return Result<void>::failure(make_project_error(a_assertContext, ProjectError::InvalidFormat,
                                                        "Descriptor extensions are not a valid JSON object"));
    }
    return Result<void>::success();
}

Result<std::string> serialize_project_descriptor(const ProjectDescriptor &a_descriptor,
                                                 const AssertContext &a_assertContext) noexcept
{
    Result<void> validation = validate_project_descriptor(a_descriptor, a_assertContext);
    if (!validation)
    {
        return Result<std::string>::failure(std::move(*validation.try_error()));
    }

    std::string output;
    output.append("{\n    \"schemaVersion\": 1,\n    \"projectId\": ");
    append_json_string(output, a_descriptor.project_id().text());
    output.append(",\n    \"displayName\": ");
    append_json_string(output, a_descriptor.display_name());
    output.append(",\n    \"engineCompatibility\": {\n        \"minimum\": \"");
    append_engine_version(output, a_descriptor.engine_compatibility().minimum);
    output.append("\",\n        \"maximumExclusive\": ");
    if (a_descriptor.engine_compatibility().maximumExclusive.has_value())
    {
        output.push_back('"');
        append_engine_version(output, *a_descriptor.engine_compatibility().maximumExclusive);
        output.push_back('"');
    }
    else
    {
        output.append("null");
    }
    output.append("\n    },\n    \"roots\": {\n        \"sourceAssets\": ");
    append_json_string(output, a_descriptor.roots().sourceAssets.text());
    output.append(",\n        \"runtimeAssets\": ");
    append_json_string(output, a_descriptor.roots().runtimeAssets.text());
    output.append(",\n        \"generated\": ");
    append_json_string(output, a_descriptor.roots().generated.text());
    output.append(",\n        \"saved\": ");
    append_json_string(output, a_descriptor.roots().saved.text());
    output.append("\n    },\n    \"defaultScene\": null,\n    \"requiredCapabilities\": [],\n    \"extensions\": ");
    output.append(a_descriptor.extensions_json());
    output.append("\n}\n");
    if (output.size() > k_maximumDescriptorBytes)
    {
        return Result<std::string>::failure(make_project_error(a_assertContext, ProjectError::InvalidFormat,
                                                               "Serialized project descriptor exceeds 1 MiB"));
    }
    return Result<std::string>::success(std::move(output));
}

Result<ProjectDescriptor> load_project_descriptor(FilesystemRoot &a_filesystem,
                                                  const AssertContext &a_assertContext) noexcept
{
    Result<RelativePath> pathResult = RelativePath::parse("CueProject.json", a_assertContext);
    if (!pathResult)
    {
        return Result<ProjectDescriptor>::failure(std::move(*pathResult.try_error()));
    }
    Result<std::vector<std::byte>> readResult =
        a_filesystem.read_file(*pathResult.try_value(), k_maximumDescriptorBytes);
    if (!readResult)
    {
        return Result<ProjectDescriptor>::failure(
            reclassify_io_error(a_assertContext, "Failed to read CueProject.json", std::move(*readResult.try_error())));
    }
    const std::vector<std::byte> &bytes = *readResult.try_value();
    const std::string_view json(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    return parse_project_descriptor(json, a_assertContext);
}

Result<void> save_project_descriptor(FilesystemRoot &a_filesystem, const ProjectDescriptor &a_descriptor,
                                     const AssertContext &a_assertContext) noexcept
{
    Result<std::string> serialization = serialize_project_descriptor(a_descriptor, a_assertContext);
    if (!serialization)
    {
        return Result<void>::failure(std::move(*serialization.try_error()));
    }
    Result<RelativePath> pathResult = RelativePath::parse("CueProject.json", a_assertContext);
    if (!pathResult)
    {
        return Result<void>::failure(std::move(*pathResult.try_error()));
    }
    const std::string &text = *serialization.try_value();
    const std::span<const char> characters(text.data(), text.size());
    Result<void> writeResult = a_filesystem.write_file_atomic(*pathResult.try_value(), std::as_bytes(characters));
    if (!writeResult)
    {
        return Result<void>::failure(reclassify_io_error(a_assertContext, "Failed to atomically save CueProject.json",
                                                         std::move(*writeResult.try_error())));
    }
    return Result<void>::success();
}
} // namespace cue
