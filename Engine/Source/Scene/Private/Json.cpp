#include "Json.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <ranges>

namespace
{
constexpr std::size_t k_maximumDepth = 64U;
constexpr std::size_t k_maximumElements = 4096U;
constexpr std::size_t k_maximumStringBytes = 256U * 1024U;

/// @brief UTF-8先頭位置から一ScalarとByte長を厳密に復号する
[[nodiscard]] bool decode_utf8(std::string_view a_text, std::size_t a_offset, std::uint32_t &a_scalar,
                               std::size_t &a_length) noexcept
{
    const auto first = static_cast<std::uint8_t>(a_text[a_offset]);
    if (first <= 0x7FU)
    {
        a_scalar = first;
        a_length = 1U;
        return true;
    }
    std::uint32_t minimum = 0U;
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

/// @brief Unicode Scalarをcanonical UTF-8として追加する
void append_utf8(std::string &a_output, std::uint32_t a_scalar)
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

/// @brief 4桁Hexadecimalを16-bit値へ変換する
[[nodiscard]] bool parse_hex(std::string_view a_text, std::size_t a_offset, std::uint32_t &a_value) noexcept
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

class Parser final
{
  public:
    /// @brief Parser寿命中だけ参照するJSON Textを束ねる
    explicit Parser(std::string_view a_input) noexcept : m_input(a_input)
    {
    }

    /// @brief 文書全体を解析して末尾Dataを拒否する
    [[nodiscard]] bool parse(cue::scene_private::JsonValue &a_value)
    {
        skip_whitespace();
        if (!parse_value(a_value, 1U))
        {
            return false;
        }
        skip_whitespace();
        return m_offset == m_input.size() || fail("JSON has trailing data");
    }

    /// @brief 最初の解析失敗理由を返す
    [[nodiscard]] std::string_view error() const noexcept
    {
        return m_error;
    }

  private:
    /// @brief 現在位置のJSON Valueを解析する
    [[nodiscard]] bool parse_value(cue::scene_private::JsonValue &a_value, std::size_t a_depth)
    {
        if (m_offset >= m_input.size())
        {
            return fail("JSON value is missing");
        }
        a_value.begin = m_offset;
        const char current = m_input[m_offset];
        bool result = false;
        if (current == '{')
        {
            result = parse_object(a_value, a_depth);
        }
        else if (current == '[')
        {
            result = parse_array(a_value, a_depth);
        }
        else if (current == '"')
        {
            a_value.type = cue::scene_private::JsonType::String;
            result = parse_string(a_value.text);
        }
        else if (current == 't' || current == 'f')
        {
            a_value.type = cue::scene_private::JsonType::Boolean;
            a_value.boolean = current == 't';
            result = consume_literal(current == 't' ? "true" : "false");
        }
        else if (current == 'n')
        {
            a_value.type = cue::scene_private::JsonType::Null;
            result = consume_literal("null");
        }
        else if (current == '-' || (current >= '0' && current <= '9'))
        {
            a_value.type = cue::scene_private::JsonType::Number;
            result = parse_number(a_value.text);
        }
        else
        {
            return fail("JSON value starts with an invalid token");
        }
        if (result)
        {
            a_value.end = m_offset;
        }
        return result;
    }

    /// @brief JSON Objectと重複Memberを上限内で解析する
    [[nodiscard]] bool parse_object(cue::scene_private::JsonValue &a_value, std::size_t a_depth)
    {
        if (a_depth > k_maximumDepth)
        {
            return fail("JSON nesting exceeds limit");
        }
        a_value.type = cue::scene_private::JsonType::Object;
        ++m_offset;
        skip_whitespace();
        if (consume('}'))
        {
            return true;
        }
        while (true)
        {
            if (a_value.members.size() >= k_maximumElements)
            {
                return fail("JSON object exceeds member limit");
            }
            std::string name;
            if (!parse_string(name) || std::ranges::any_of(a_value.members, [&name](const auto &a_member) noexcept
                                                           { return a_member.first == name; }))
            {
                return fail("JSON object member is invalid or duplicated");
            }
            skip_whitespace();
            if (!consume(':'))
            {
                return fail("JSON object member is missing ':'");
            }
            skip_whitespace();
            cue::scene_private::JsonValue value;
            if (!parse_value(value, a_depth + 1U))
            {
                return false;
            }
            a_value.members.emplace_back(std::move(name), std::move(value));
            skip_whitespace();
            if (consume('}'))
            {
                return true;
            }
            if (!consume(','))
            {
                return fail("JSON object member is missing ','");
            }
            skip_whitespace();
        }
    }

    /// @brief JSON Arrayを要素上限内で解析する
    [[nodiscard]] bool parse_array(cue::scene_private::JsonValue &a_value, std::size_t a_depth)
    {
        if (a_depth > k_maximumDepth)
        {
            return fail("JSON nesting exceeds limit");
        }
        a_value.type = cue::scene_private::JsonType::Array;
        ++m_offset;
        skip_whitespace();
        if (consume(']'))
        {
            return true;
        }
        while (true)
        {
            if (a_value.elements.size() >= k_maximumElements)
            {
                return fail("JSON array exceeds element limit");
            }
            cue::scene_private::JsonValue value;
            if (!parse_value(value, a_depth + 1U))
            {
                return false;
            }
            a_value.elements.push_back(std::move(value));
            skip_whitespace();
            if (consume(']'))
            {
                return true;
            }
            if (!consume(','))
            {
                return fail("JSON array element is missing ','");
            }
            skip_whitespace();
        }
    }

    /// @brief JSON StringをUTF-8へ復号する
    [[nodiscard]] bool parse_string(std::string &a_output)
    {
        if (!consume('"'))
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
                    return fail("JSON string has a control byte");
                }
                std::uint32_t scalar = 0U;
                std::size_t length = 0U;
                if (!decode_utf8(m_input, m_offset, scalar, length))
                {
                    return fail("JSON string has invalid UTF-8");
                }
                a_output.append(m_input.substr(m_offset, length));
                m_offset += length;
            }
            if (a_output.size() > k_maximumStringBytes)
            {
                return fail("JSON string exceeds byte limit");
            }
        }
        return fail("JSON string is missing closing quote");
    }

    /// @brief 一つのJSON Escapeを復号する
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
            return fail("JSON escape is invalid");
        }
        std::uint32_t first = 0U;
        if (!parse_hex(m_input, m_offset, first))
        {
            return fail("JSON unicode escape is invalid");
        }
        m_offset += 4U;
        std::uint32_t scalar = first;
        if (first >= 0xD800U && first <= 0xDBFFU)
        {
            if (m_offset + 6U > m_input.size() || m_input.substr(m_offset, 2U) != "\\u")
            {
                return fail("JSON surrogate pair is incomplete");
            }
            std::uint32_t second = 0U;
            if (!parse_hex(m_input, m_offset + 2U, second) || second < 0xDC00U || second > 0xDFFFU)
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
        append_utf8(a_output, scalar);
        return true;
    }

    /// @brief RFC 8259 Numberを原文のまま保持する
    [[nodiscard]] bool parse_number(std::string &a_output)
    {
        const std::size_t start = m_offset;
        (void)consume('-');
        if (consume('0'))
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
        if (consume('.') && !consume_digits())
        {
            return fail("JSON number has no fraction digits");
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
                return fail("JSON number exponent has no digits");
            }
        }
        a_output.assign(m_input.substr(start, m_offset - start));
        return true;
    }

    /// @brief 一つ以上のDecimal Digitを消費する
    [[nodiscard]] bool consume_digits() noexcept
    {
        const std::size_t start = m_offset;
        while (m_offset < m_input.size() && m_input[m_offset] >= '0' && m_input[m_offset] <= '9')
        {
            ++m_offset;
        }
        return start != m_offset;
    }

    /// @brief 固定Literalを消費する
    [[nodiscard]] bool consume_literal(std::string_view a_literal) noexcept
    {
        if (m_input.substr(m_offset, a_literal.size()) != a_literal)
        {
            return fail("JSON literal is invalid");
        }
        m_offset += a_literal.size();
        return true;
    }

    /// @brief 期待文字を一つだけ消費する
    [[nodiscard]] bool consume(char a_character) noexcept
    {
        if (m_offset < m_input.size() && m_input[m_offset] == a_character)
        {
            ++m_offset;
            return true;
        }
        return false;
    }

    /// @brief JSON Whitespaceを読み飛ばす
    void skip_whitespace() noexcept
    {
        while (m_offset < m_input.size() && (m_input[m_offset] == ' ' || m_input[m_offset] == '\t' ||
                                             m_input[m_offset] == '\r' || m_input[m_offset] == '\n'))
        {
            ++m_offset;
        }
    }

    /// @brief 最初のErrorだけを保存してfalseを返す
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
} // namespace

namespace cue::scene_private
{
bool parse_json_document(std::string_view a_input, JsonValue &a_value, std::string_view &a_error)
{
    Parser parser(a_input);
    const bool result = parser.parse(a_value);
    a_error = parser.error();
    return result;
}

bool is_valid_json_string_text(std::string_view a_text) noexcept
{
    if (a_text.size() > k_maximumStringBytes)
    {
        return false;
    }

    std::size_t offset = 0U;
    while (offset < a_text.size())
    {
        std::uint32_t scalar = 0U;
        std::size_t length = 0U;
        if (!decode_utf8(a_text, offset, scalar, length))
        {
            return false;
        }
        offset += length;
    }
    return true;
}

const JsonValue *find_json_member(const JsonValue &a_object, std::string_view a_name) noexcept
{
    const auto found = std::ranges::find_if(a_object.members, [a_name](const auto &a_member) noexcept
                                            { return a_member.first == a_name; });
    return found == a_object.members.end() ? nullptr : &found->second;
}

void append_json_string(std::string &a_output, std::string_view a_text)
{
    constexpr std::array<char, 16U> digits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                              '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
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
                a_output.push_back(digits[(character >> 4U) & 0x0FU]);
                a_output.push_back(digits[character & 0x0FU]);
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
            if (index > 0U)
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
            if (index > 0U)
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
} // namespace cue::scene_private
