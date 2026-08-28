// Repository所有C++を字句解析し、Cue.Math境界とDirectXMath非依存を検証する

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
enum class TokenKind
{
    Identifier,
    Scope,
    Equal,
    Other,
};

struct Token final
{
    TokenKind kind;
    std::string_view text;
};

/// @brief ASCII英字を小文字へ変換して返す
[[nodiscard]] char to_lower_ascii(char a_value) noexcept
{
    if (a_value >= 'A' && a_value <= 'Z')
    {
        return static_cast<char>(a_value - 'A' + 'a');
    }

    return a_value;
}

/// @brief 文字列をASCII小文字へ変換して返す
[[nodiscard]] std::string lower_ascii(std::string_view a_value)
{
    std::string result(a_value);
    std::transform(result.begin(), result.end(), result.begin(), to_lower_ascii);
    return result;
}

/// @brief C++識別子の先頭に使用できる文字か判定する
[[nodiscard]] bool is_identifier_start(char a_value) noexcept
{
    const auto value = static_cast<unsigned char>(a_value);
    return a_value == '_' || std::isalpha(value) != 0;
}

/// @brief C++識別子の継続に使用できる文字か判定する
[[nodiscard]] bool is_identifier_continue(char a_value) noexcept
{
    const auto value = static_cast<unsigned char>(a_value);
    return a_value == '_' || std::isalnum(value) != 0;
}

/// @brief Backslash直後の物理改行を除去してC++論理行へ変換する
[[nodiscard]] std::string splice_cpp_lines(std::string_view a_source)
{
    std::string result;
    result.reserve(a_source.size());

    for (std::size_t index = 0; index < a_source.size();)
    {
        if (a_source[index] == '\\' && index + 1U < a_source.size())
        {
            if (a_source[index + 1U] == '\n')
            {
                index += 2U;
                continue;
            }

            if (a_source[index + 1U] == '\r' && index + 2U < a_source.size() &&
                a_source[index + 2U] == '\n')
            {
                index += 3U;
                continue;
            }
        }

        result.push_back(a_source[index]);
        ++index;
    }

    return result;
}

/// @brief C++ Raw String Prefix位置から対応Delimiterの終端直後を返す
[[nodiscard]] std::optional<std::size_t> find_raw_string_end(
    std::string_view a_source, std::size_t a_start) noexcept
{
    constexpr std::array<std::string_view, 5> prefixes = {
        "R\"",
        "u8R\"",
        "uR\"",
        "UR\"",
        "LR\"",
    };

    std::string_view prefix;

    for (const auto candidate : prefixes)
    {
        if (a_source.substr(a_start, candidate.size()) == candidate)
        {
            prefix = candidate;
            break;
        }
    }

    if (prefix.empty() ||
        (a_start > 0U && is_identifier_continue(a_source[a_start - 1U])))
    {
        return std::nullopt;
    }

    const auto delimiterStart = a_start + prefix.size();
    auto openParenthesis = delimiterStart;

    while (openParenthesis < a_source.size() &&
           a_source[openParenthesis] != '(' &&
           openParenthesis - delimiterStart <= 16U)
    {
        const auto value = a_source[openParenthesis];

        if (value == ' ' || value == ')' || value == '\\' || value == '\t' ||
            value == '\r' || value == '\n')
        {
            return std::nullopt;
        }

        ++openParenthesis;
    }

    if (openParenthesis >= a_source.size() ||
        a_source[openParenthesis] != '(' ||
        openParenthesis - delimiterStart > 16U)
    {
        return std::nullopt;
    }

    const auto delimiter =
        a_source.substr(delimiterStart, openParenthesis - delimiterStart);
    std::string terminator = ")";
    terminator.append(delimiter);
    terminator.push_back('"');

    const auto terminatorStart = a_source.find(terminator, openParenthesis + 1U);

    if (terminatorStart == std::string_view::npos)
    {
        return std::nullopt;
    }

    return terminatorStart + terminator.size();
}

/// @brief 指定範囲を改行だけ保持した空白へ置換して出力へ追加する
void append_hidden_range(std::string &a_output, std::string_view a_source,
                         std::size_t a_begin, std::size_t a_end)
{
    for (auto index = a_begin; index < a_end; ++index)
    {
        const auto value = a_source[index];
        a_output.push_back(value == '\r' || value == '\n' ? value : ' ');
    }
}

/// @brief 文字列の先頭と末尾のASCII空白を除いたViewを返す
[[nodiscard]] std::string_view trim_ascii(std::string_view a_value) noexcept;

/// @brief 文字列先頭のASCII空白だけを除いたViewを返す
[[nodiscard]] std::string_view trim_ascii_left(
    std::string_view a_value) noexcept
{
    while (!a_value.empty() &&
           std::isspace(static_cast<unsigned char>(a_value.front())) != 0)
    {
        a_value.remove_prefix(1U);
    }

    return a_value;
}

/// @brief 現在行がQuote形式Include Operand直前の場合にtrueを返す
[[nodiscard]] bool is_include_operand_position(std::string_view a_code) noexcept
{
    const auto lineStart = a_code.find_last_of("\r\n");
    const auto line = a_code.substr(
        lineStart == std::string_view::npos ? 0U : lineStart + 1U);
    std::size_t index = 0U;

    while (index < line.size() && (line[index] == ' ' || line[index] == '\t'))
    {
        ++index;
    }

    if (index < line.size() && line[index] == '#')
    {
        ++index;
    }
    else if (line.substr(index, 2U) == "%:")
    {
        index += 2U;
    }
    else
    {
        return false;
    }

    while (index < line.size() && (line[index] == ' ' || line[index] == '\t'))
    {
        ++index;
    }

    constexpr std::string_view includeName = "include";

    if (line.substr(index, includeName.size()) != includeName)
    {
        return false;
    }

    index += includeName.size();

    if (index < line.size() && is_identifier_continue(line[index]))
    {
        return false;
    }

    while (index < line.size() && (line[index] == ' ' || line[index] == '\t'))
    {
        ++index;
    }

    return index == line.size();
}

/// @brief 現在行がQuote形式Header Unit Import直前の場合にtrueを返す
[[nodiscard]] bool is_import_operand_position(std::string_view a_code) noexcept
{
    const auto lineStart = a_code.find_last_of("\r\n");
    auto line = trim_ascii_left(a_code.substr(
        lineStart == std::string_view::npos ? 0U : lineStart + 1U));
    constexpr std::string_view exportName = "export";
    constexpr std::string_view importName = "import";

    if (line.substr(0U, exportName.size()) == exportName &&
        line.size() > exportName.size() &&
        std::isspace(static_cast<unsigned char>(line[exportName.size()])) != 0)
    {
        line = trim_ascii_left(line.substr(exportName.size()));
    }

    if (line.substr(0U, importName.size()) != importName ||
        (line.size() > importName.size() &&
         std::isspace(static_cast<unsigned char>(line[importName.size()])) == 0))
    {
        return false;
    }

    return trim_ascii(line.substr(importName.size())).empty();
}

/// @brief 現在行がQuote形式Header依存のOperand直前の場合にtrueを返す
[[nodiscard]] bool is_header_operand_position(std::string_view a_code) noexcept
{
    return is_include_operand_position(a_code) ||
           is_import_operand_position(a_code);
}

/// @brief 現在行がMacro DefinitionのReplacement List直前の場合にtrueを返す
[[nodiscard]] bool is_define_replacement_position(
    std::string_view a_code) noexcept
{
    const auto lineStart = a_code.find_last_of("\r\n");
    auto line = trim_ascii_left(a_code.substr(
        lineStart == std::string_view::npos ? 0U : lineStart + 1U));

    if (!line.empty() && line.front() == '#')
    {
        line.remove_prefix(1U);
    }
    else if (line.substr(0U, 2U) == "%:")
    {
        line.remove_prefix(2U);
    }
    else
    {
        return false;
    }

    line = trim_ascii_left(line);
    constexpr std::string_view defineName = "define";

    if (line.substr(0U, defineName.size()) != defineName ||
        line.size() <= defineName.size() ||
        std::isspace(static_cast<unsigned char>(line[defineName.size()])) == 0)
    {
        return false;
    }

    line = trim_ascii_left(line.substr(defineName.size()));

    if (line.empty() || !is_identifier_start(line.front()))
    {
        return false;
    }

    while (!line.empty() && is_identifier_continue(line.front()))
    {
        line.remove_prefix(1U);
    }

    if (!line.empty() && line.front() == '(')
    {
        const auto close = line.find(')');

        if (close == std::string_view::npos)
        {
            return false;
        }

        line.remove_prefix(close + 1U);
    }

    return trim_ascii(line).empty();
}

/// @brief Quoteまたは文字Literalの終端直後を返す
[[nodiscard]] std::size_t find_quoted_literal_end(
    std::string_view a_source, std::size_t a_start, char a_quote) noexcept
{
    auto index = a_start + 1U;

    while (index < a_source.size())
    {
        if (a_source[index] == '\\' && index + 1U < a_source.size())
        {
            index += 2U;
            continue;
        }

        if (a_source[index] == a_quote)
        {
            return index + 1U;
        }

        ++index;
    }

    return a_source.size();
}

/// @brief コメントとLiteralを除去しQuote形式IncludeだけをAngle形式で保持する
[[nodiscard]] std::string sanitize_cpp_source(std::string_view a_source)
{
    const auto source = splice_cpp_lines(a_source);
    std::string result;
    result.reserve(source.size());

    for (std::size_t index = 0U; index < source.size();)
    {
        if (const auto rawEnd = find_raw_string_end(source, index))
        {
            append_hidden_range(result, source, index, *rawEnd);
            index = *rawEnd;
            continue;
        }

        if (source.substr(index, 2U) == "//")
        {
            const auto lineEnd = source.find_first_of("\r\n", index + 2U);
            const auto end =
                lineEnd == std::string::npos ? source.size() : lineEnd;
            append_hidden_range(result, source, index, end);
            index = end;
            continue;
        }

        if (source.substr(index, 2U) == "/*")
        {
            const auto close = source.find("*/", index + 2U);
            const auto end =
                close == std::string::npos ? source.size() : close + 2U;
            append_hidden_range(result, source, index, end);
            index = end;
            continue;
        }

        if (source[index] == '"')
        {
            const auto end = find_quoted_literal_end(source, index, '"');

            if ((is_header_operand_position(result) ||
                 is_define_replacement_position(result)) &&
                end > index + 1U)
            {
                result.push_back('<');
                result.append(source.substr(index + 1U, end - index - 2U));
                result.push_back('>');
            }
            else
            {
                append_hidden_range(result, source, index, end);
            }

            index = end;
            continue;
        }

        if (source[index] == '\'')
        {
            const auto end = find_quoted_literal_end(source, index, '\'');
            append_hidden_range(result, source, index, end);
            index = end;
            continue;
        }

        result.push_back(source[index]);
        ++index;
    }

    return result;
}

/// @brief Sanitized SourceがMacro Token Paste Operatorを含む場合にtrueを返す
[[nodiscard]] bool has_macro_token_paste(std::string_view a_code) noexcept
{
    return a_code.find("##") != std::string_view::npos ||
           a_code.find("%:%:") != std::string_view::npos;
}

/// @brief 文字列が識別子境界付きASCII名を含む場合にtrueを返す
[[nodiscard]] bool contains_ascii_identifier(
    std::string_view a_value, std::string_view a_identifier) noexcept
{
    auto offset = a_value.find(a_identifier);

    while (offset != std::string_view::npos)
    {
        const bool hasLeftBoundary =
            offset == 0U || !is_identifier_continue(a_value[offset - 1U]);
        const auto end = offset + a_identifier.size();
        const bool hasRightBoundary =
            end == a_value.size() || !is_identifier_continue(a_value[end]);

        if (hasLeftBoundary && hasRightBoundary)
        {
            return true;
        }

        offset = a_value.find(a_identifier, offset + 1U);
    }

    return false;
}

/// @brief PathがRepository Root直下の生成物または管理Directoryか判定する
[[nodiscard]] bool is_excluded_root_directory(
    const std::filesystem::path &a_relativePath) noexcept
{
    const auto iterator = a_relativePath.begin();

    if (iterator == a_relativePath.end())
    {
        return false;
    }

    const auto name = lower_ascii(iterator->string());
    return name == ".git" || name == ".vs" || name == "out" || name == "build";
}

/// @brief PathがRepository所有C++ Source拡張子を持つ場合にtrueを返す
[[nodiscard]] bool is_cpp_source_path(const std::filesystem::path &a_path)
{
    constexpr std::array<std::string_view, 15> extensions = {
        ".c++", ".cc", ".cpp", ".cppm", ".cxx", ".cxxm", ".h", ".h++",
        ".hh",  ".hpp", ".hxx", ".inl",  ".ipp", ".ixx",  ".tpp",
    };
    const auto extension = lower_ascii(a_path.extension().string());
    return std::find(extensions.begin(), extensions.end(), extension) !=
           extensions.end();
}

/// @brief C++ Source文字列を検査用Token列へ分割する
[[nodiscard]] std::vector<Token> tokenize_cpp(std::string_view a_code)
{
    std::vector<Token> tokens;

    for (std::size_t index = 0U; index < a_code.size();)
    {
        if (is_identifier_start(a_code[index]))
        {
            const auto start = index++;

            while (index < a_code.size() &&
                   is_identifier_continue(a_code[index]))
            {
                ++index;
            }

            tokens.push_back(Token{
                TokenKind::Identifier,
                a_code.substr(start, index - start),
            });
            continue;
        }

        if (a_code.substr(index, 2U) == "::")
        {
            tokens.push_back(Token{TokenKind::Scope, a_code.substr(index, 2U)});
            index += 2U;
            continue;
        }

        if (a_code[index] == '=')
        {
            tokens.push_back(Token{TokenKind::Equal, a_code.substr(index, 1U)});
        }
        else if (!std::isspace(static_cast<unsigned char>(a_code[index])))
        {
            tokens.push_back(Token{TokenKind::Other, a_code.substr(index, 1U)});
        }

        ++index;
    }

    return tokens;
}

/// @brief Tokenが指定Prefixで始まる場合にtrueを返す
[[nodiscard]] bool starts_with(std::string_view a_value,
                               std::string_view a_prefix) noexcept
{
    return a_value.substr(0U, a_prefix.size()) == a_prefix;
}

/// @brief TokenのSuffixが大文字英数字とUnderscoreだけの場合にtrueを返す
[[nodiscard]] bool has_upper_identifier_suffix(
    std::string_view a_value, std::size_t a_prefixLength) noexcept
{
    for (auto index = a_prefixLength; index < a_value.size(); ++index)
    {
        const auto value = a_value[index];

        if (!((value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_'))
        {
            return false;
        }
    }

    return true;
}

/// @brief TokenがDirectXMath固有識別子の場合にtrueを返す
[[nodiscard]] bool is_directxmath_identifier(std::string_view a_value) noexcept
{
    constexpr std::array<std::string_view, 6> aliases = {
        "FXMVECTOR",
        "GXMVECTOR",
        "HXMVECTOR",
        "CXMVECTOR",
        "FXMMATRIX",
        "CXMMATRIX",
    };
    constexpr std::array<std::string_view, 9> exactIdentifiers = {
        "XM256_STREAM_PS",
        "XMASSERT",
        "XMFINLINE",
        "XMGLOBALCONST",
        "XMGLOBALCONSTEX",
        "XMINLINE",
        "XMCONSTEXPR",
        "XMMax",
        "XMMin",
    };
    constexpr std::array<std::string_view, 21> upperPrefixes = {
        "XMFLOAT", "XMINT",   "XMUINT", "XMBYTE",  "XMUBYTE", "XMSHORT",
        "XMUSHORT", "XMHALF", "XMCOLOR", "XMHENDN", "XMDEC",   "XMUDEC",
        "XMXDEC",  "XMUXDEC", "XMUNIBBLE", "XMU555", "XMU565", "XMVECTOR",
        "XMMATRIX", "XMUNORM", "XMSNORM",
    };
    constexpr std::array<std::string_view, 15> functionPrefixes = {
        "XMVector", "XMMatrix", "XMQuaternion", "XMPlane", "XMColor",
        "XMScalar", "XMConvert", "XMLoad", "XMStore", "XMVerify",
        "XMComparison", "XMSinCos", "XMFresnel", "XMMin", "XMMax",
    };

    if (std::find(aliases.begin(), aliases.end(), a_value) != aliases.end() ||
        std::find(exactIdentifiers.begin(), exactIdentifiers.end(), a_value) !=
            exactIdentifiers.end() ||
        starts_with(a_value, "XM_") || starts_with(a_value, "g_XM"))
    {
        return true;
    }

    for (const auto prefix : upperPrefixes)
    {
        if (starts_with(a_value, prefix) &&
            has_upper_identifier_suffix(a_value, prefix.size()))
        {
            return true;
        }
    }

    for (const auto prefix : functionPrefixes)
    {
        if (starts_with(a_value, prefix))
        {
            return true;
        }
    }

    return false;
}

/// @brief DirectX Namespace配下のDirectXMath Family名か判定する
[[nodiscard]] bool is_directxmath_namespace_member(
    std::string_view a_value) noexcept
{
    constexpr std::array<std::string_view, 11> companionNames = {
        "PackedVector",
        "Colors",
        "ColorsLinear",
        "BoundingSphere",
        "BoundingBox",
        "BoundingOrientedBox",
        "BoundingFrustum",
        "ContainmentType",
        "PlaneIntersectionType",
        "TriangleTests",
        "XM",
    };

    return is_directxmath_identifier(a_value) ||
           std::find(companionNames.begin(), companionNames.end(), a_value) !=
               companionNames.end();
}

/// @brief Token列がDirectXMath型・Macro・Namespace依存を含むか判定する
[[nodiscard]] bool has_forbidden_directxmath_tokens(
    const std::vector<Token> &a_tokens) noexcept
{
    for (std::size_t index = 0U; index < a_tokens.size(); ++index)
    {
        const auto &token = a_tokens[index];

        if (token.kind == TokenKind::Identifier &&
            (token.text == "DirectX" ||
             is_directxmath_identifier(token.text)))
        {
            return true;
        }

        if (index + 2U < a_tokens.size() &&
            a_tokens[index].kind == TokenKind::Identifier &&
            a_tokens[index].text == "using" &&
            a_tokens[index + 1U].kind == TokenKind::Identifier &&
            a_tokens[index + 1U].text == "namespace")
        {
            auto targetIndex = index + 2U;

            if (a_tokens[targetIndex].kind == TokenKind::Scope)
            {
                ++targetIndex;
            }

            if (targetIndex < a_tokens.size() &&
                a_tokens[targetIndex].kind == TokenKind::Identifier &&
                a_tokens[targetIndex].text == "DirectX")
            {
                return true;
            }
        }

        if (index + 3U < a_tokens.size() &&
            a_tokens[index].kind == TokenKind::Identifier &&
            a_tokens[index].text == "namespace" &&
            a_tokens[index + 1U].kind == TokenKind::Identifier &&
            a_tokens[index + 2U].kind == TokenKind::Equal)
        {
            auto targetIndex = index + 3U;

            if (targetIndex < a_tokens.size() &&
                a_tokens[targetIndex].kind == TokenKind::Scope)
            {
                ++targetIndex;
            }

            if (targetIndex < a_tokens.size() &&
                a_tokens[targetIndex].kind == TokenKind::Identifier &&
                a_tokens[targetIndex].text == "DirectX")
            {
                return true;
            }
        }

        if (index + 2U < a_tokens.size() &&
            a_tokens[index].kind == TokenKind::Identifier &&
            a_tokens[index].text == "DirectX" &&
            a_tokens[index + 1U].kind == TokenKind::Scope &&
            a_tokens[index + 2U].kind == TokenKind::Identifier &&
            is_directxmath_namespace_member(a_tokens[index + 2U].text))
        {
            return true;
        }
    }

    return false;
}

/// @brief 文字列の先頭と末尾のASCII空白を除いたViewを返す
[[nodiscard]] std::string_view trim_ascii(std::string_view a_value) noexcept
{
    while (!a_value.empty() &&
           std::isspace(static_cast<unsigned char>(a_value.front())) != 0)
    {
        a_value.remove_prefix(1U);
    }

    while (!a_value.empty() &&
           std::isspace(static_cast<unsigned char>(a_value.back())) != 0)
    {
        a_value.remove_suffix(1U);
    }

    return a_value;
}

/// @brief Include Header名がDirectXMath Familyの場合にtrueを返す
[[nodiscard]] bool is_directxmath_header(std::string_view a_header)
{
    constexpr std::array<std::string_view, 4> headers = {
        "directxmath.h",
        "directxpackedvector.h",
        "directxcollision.h",
        "directxcolors.h",
    };
    std::string portableHeader(trim_ascii(a_header));
    std::replace(portableHeader.begin(), portableHeader.end(), '\\', '/');
    const auto normalized = lower_ascii(
        std::filesystem::path(portableHeader).filename().string());
    return std::find(headers.begin(), headers.end(), normalized) != headers.end();
}

/// @brief Cue.Mathが依存できないHeader名の場合にtrueを返す
[[nodiscard]] bool is_forbidden_math_header(std::string_view a_header)
{
    std::string portableHeader(trim_ascii(a_header));
    std::replace(portableHeader.begin(), portableHeader.end(), '\\', '/');
    const auto normalized = lower_ascii(portableHeader);
    const auto fileName = lower_ascii(
        std::filesystem::path(portableHeader).filename().string());
    return is_directxmath_header(normalized) || fileName == "windows.h" ||
           fileName == "d3d12.h" || fileName == "intrin.h" ||
           starts_with(normalized, "cue/platform/") ||
           starts_with(normalized, "cue/rhi/") ||
           starts_with(normalized, "cue/runtimehost/") ||
           starts_with(normalized, "cue/editor/");
}

/// @brief Math SourceからのInclude先をRepository相対Pathへ解決し禁止Moduleか判定する
[[nodiscard]] bool is_forbidden_math_include_path(
    std::string_view a_header,
    const std::filesystem::path &a_sourceRelativePath)
{
    std::string portableHeader(a_header);
    std::replace(portableHeader.begin(), portableHeader.end(), '\\', '/');
    const auto resolved =
        (a_sourceRelativePath.parent_path() / portableHeader).lexically_normal();
    const auto normalized = lower_ascii(resolved.generic_string());
    return starts_with(normalized, "engine/source/platform/") ||
           starts_with(normalized, "engine/source/rhi/") ||
           starts_with(normalized, "engine/source/runtimehost/") ||
           starts_with(normalized, "engine/source/editor/");
}

/// @brief Header OperandがMath依存規則に違反する場合に診断してfalseを返す
[[nodiscard]] bool validate_header_operand(
    std::string_view a_header, bool a_isMathSource,
    const std::filesystem::path &a_path, bool a_reportsFailure)
{
    if (!is_directxmath_header(a_header) &&
        (!a_isMathSource ||
         (!is_forbidden_math_header(a_header) &&
          !is_forbidden_math_include_path(a_header, a_path))))
    {
        return true;
    }

    if (a_reportsFailure)
    {
        std::cerr << "Forbidden header dependency: " << a_path.string()
                  << '\n';
    }

    return false;
}

/// @brief Sanitized SourceのInclude DirectiveとMacro Operandを検証する
[[nodiscard]] bool validate_include_directives(
    std::string_view a_code, bool a_isMathSource,
    const std::filesystem::path &a_path, bool a_reportsFailure = true)
{
    if (contains_ascii_identifier(a_code, "__pragma"))
    {
        if (a_reportsFailure)
        {
            std::cerr << "MSVC __pragma is forbidden: "
                      << a_path.string() << '\n';
        }

        return false;
    }

    std::size_t lineStart = 0U;

    while (lineStart <= a_code.size())
    {
        const auto lineEnd = a_code.find_first_of("\r\n", lineStart);
        auto line = trim_ascii(a_code.substr(
            lineStart,
            lineEnd == std::string_view::npos ? std::string_view::npos
                                              : lineEnd - lineStart));
        std::size_t index = 0U;

        if (!line.empty() && line[index] == '#')
        {
            ++index;
        }
        else if (line.substr(index, 2U) == "%:")
        {
            index += 2U;
        }
        else
        {
            index = line.size();
        }

        if (index < line.size())
        {
            while (index < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[index])) != 0)
            {
                ++index;
            }

            constexpr std::string_view includeName = "include";
            const auto directiveStart = index;
            constexpr std::string_view pragmaName = "pragma";

            if (line.substr(index, pragmaName.size()) == pragmaName &&
                index + pragmaName.size() < line.size() &&
                std::isspace(static_cast<unsigned char>(
                    line[index + pragmaName.size()])) != 0)
            {
                const auto pragmaOperand = trim_ascii_left(
                    line.substr(index + pragmaName.size()));
                constexpr std::string_view includeAliasName = "include_alias";

                if (pragmaOperand.substr(0U, includeAliasName.size()) ==
                        includeAliasName &&
                    (pragmaOperand.size() == includeAliasName.size() ||
                     !is_identifier_continue(
                         pragmaOperand[includeAliasName.size()])))
                {
                    if (a_reportsFailure)
                    {
                        std::cerr << "Include alias is forbidden: "
                                  << a_path.string() << '\n';
                    }

                    return false;
                }
            }

            if (line.substr(index, includeName.size()) == includeName &&
                (index + includeName.size() == line.size() ||
                 !is_identifier_continue(line[index + includeName.size()])))
            {
                index += includeName.size();

                while (index < line.size() &&
                       std::isspace(static_cast<unsigned char>(line[index])) != 0)
                {
                    ++index;
                }

                if (index >= line.size() || line[index] != '<')
                {
                    if (a_reportsFailure)
                    {
                        std::cerr << "Macro include operand is forbidden: "
                                  << a_path.string() << '\n';
                    }

                    return false;
                }

                const auto close = line.find('>', index + 1U);

                if (close == std::string_view::npos)
                {
                    if (a_reportsFailure)
                    {
                        std::cerr << "Unterminated include operand: "
                                  << a_path.string() << '\n';
                    }

                    return false;
                }

                const auto header = line.substr(index + 1U, close - index - 1U);

                if (!validate_header_operand(
                        header, a_isMathSource, a_path, a_reportsFailure))
                {
                    return false;
                }
            }

            constexpr std::string_view defineName = "define";

            if (line.substr(directiveStart, defineName.size()) == defineName &&
                directiveStart + defineName.size() < line.size() &&
                std::isspace(static_cast<unsigned char>(
                    line[directiveStart + defineName.size()])) != 0)
            {
                auto definition = trim_ascii_left(
                    line.substr(directiveStart + defineName.size()));
                while (!definition.empty() &&
                       is_identifier_continue(definition.front()))
                {
                    definition.remove_prefix(1U);
                }

                const bool isFunctionLike =
                    !definition.empty() && definition.front() == '(';

                if (isFunctionLike)
                {
                    const auto close = definition.find(')');

                    if (close != std::string_view::npos)
                    {
                        definition.remove_prefix(close + 1U);
                    }
                }

                auto replacement = trim_ascii_left(definition);

                if (replacement.substr(0U, 2U) == "::")
                {
                    replacement = trim_ascii_left(replacement.substr(2U));
                }

                constexpr std::string_view directXName = "DirectX";

                if (replacement.substr(0U, directXName.size()) == directXName &&
                    (replacement.size() == directXName.size() ||
                     !is_identifier_continue(replacement[directXName.size()])))
                {
                    if (a_reportsFailure)
                    {
                        std::cerr << "DirectX namespace macro alias is forbidden: "
                                  << a_path.string() << '\n';
                    }

                    return false;
                }

                if (!replacement.empty() && replacement.front() == '<')
                {
                    const auto close = replacement.find('>', 1U);

                    if (close == std::string_view::npos)
                    {
                        if (a_reportsFailure)
                        {
                            std::cerr << "Unterminated macro header operand: "
                                      << a_path.string() << '\n';
                        }

                        return false;
                    }

                    if (!isFunctionLike &&
                        !validate_header_operand(
                            replacement.substr(1U, close - 1U),
                            a_isMathSource, a_path, false))
                    {
                        if (a_reportsFailure)
                        {
                            std::cerr << "Forbidden header dependency: "
                                      << a_path.string() << '\n';
                        }

                        return false;
                    }
                }
            }
        }

        auto importLine = line;
        constexpr std::string_view exportName = "export";
        constexpr std::string_view importName = "import";

        if (importLine.substr(0U, exportName.size()) == exportName &&
            importLine.size() > exportName.size() &&
            std::isspace(static_cast<unsigned char>(
                importLine[exportName.size()])) != 0)
        {
            importLine = trim_ascii(importLine.substr(exportName.size()));
        }

        if (importLine.substr(0U, importName.size()) == importName &&
            importLine.size() > importName.size() &&
            (std::isspace(static_cast<unsigned char>(
                 importLine[importName.size()])) != 0 ||
             importLine[importName.size()] == '<'))
        {
            const auto operand = trim_ascii(importLine.substr(importName.size()));

            if (!operand.empty() && operand.front() == '<')
            {
                const auto close = operand.find('>', 1U);

                if (close == std::string_view::npos)
                {
                    if (a_reportsFailure)
                    {
                        std::cerr << "Unterminated import operand: "
                                  << a_path.string() << '\n';
                    }

                    return false;
                }

                if (!validate_header_operand(
                        operand.substr(1U, close - 1U), a_isMathSource,
                        a_path, a_reportsFailure))
                {
                    return false;
                }
            }
            else if (!operand.empty() &&
                     is_identifier_start(operand.front()))
            {
                std::size_t macroNameLength = 1U;

                while (macroNameLength < operand.size() &&
                       is_identifier_continue(operand[macroNameLength]))
                {
                    ++macroNameLength;
                }

                const auto suffix = trim_ascii_left(
                    operand.substr(macroNameLength));

                if (!suffix.empty() && suffix.front() == '(')
                {
                    if (a_reportsFailure)
                    {
                        std::cerr << "Macro header import operand is forbidden: "
                                  << a_path.string() << '\n';
                    }

                    return false;
                }
            }
        }

        if (lineEnd == std::string_view::npos)
        {
            break;
        }

        lineStart = lineEnd + 1U;

        if (lineEnd + 1U < a_code.size() && a_code[lineEnd] == '\r' &&
            a_code[lineEnd + 1U] == '\n')
        {
            ++lineStart;
        }
    }

    return true;
}

/// @brief Relative PathがCue.Math Source配下の場合にtrueを返す
[[nodiscard]] bool is_math_source_path(
    const std::filesystem::path &a_relativePath)
{
    const auto normalized = lower_ascii(a_relativePath.generic_string());
    return starts_with(normalized, "engine/source/math/");
}

/// @brief Repository所有C++ Fileを読み取り依存規則へ適合するか検証する
[[nodiscard]] bool validate_cpp_file(
    const std::filesystem::path &a_repositoryRoot,
    const std::filesystem::path &a_path)
{
    std::ifstream stream(a_path, std::ios::binary);

    if (!stream)
    {
        std::cerr << "Could not read C++ source: " << a_path.string() << '\n';
        return false;
    }

    const std::string source{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };

    const auto code = sanitize_cpp_source(source);
    std::error_code relativeError;
    const auto relativePath =
        std::filesystem::relative(a_path, a_repositoryRoot, relativeError);

    if (relativeError)
    {
        std::cerr << "Could not resolve repository-relative path: "
                  << a_path.string() << '\n';
        return false;
    }

    if (!validate_include_directives(
            code, is_math_source_path(relativePath), relativePath))
    {
        return false;
    }

    if (has_macro_token_paste(code))
    {
        std::cerr << "Macro token paste can hide forbidden dependencies: "
                  << relativePath.string() << '\n';
        return false;
    }

    if (has_forbidden_directxmath_tokens(tokenize_cpp(code)))
    {
        std::cerr << "Forbidden DirectXMath token dependency: "
                  << relativePath.string() << '\n';
        return false;
    }

    return true;
}

/// @brief Build PropertyがForced Include Optionを含む場合にtrueを返す
[[nodiscard]] bool has_forced_include_option(
    std::string_view a_manifest)
{
    std::size_t lineStart = 0U;

    while (lineStart < a_manifest.size())
    {
        const auto lineEnd = a_manifest.find('\n', lineStart);
        const auto line = trim_ascii(a_manifest.substr(
            lineStart,
            lineEnd == std::string_view::npos
                ? a_manifest.size() - lineStart
                : lineEnd - lineStart));
        const auto assignment = line.find('=');

        if (assignment != std::string_view::npos)
        {
            const auto property = line.substr(0U, assignment);
            auto normalized = lower_ascii(line.substr(assignment + 1U));
            const bool isVisualStudioSetting =
                property.ends_with(".VS_SETTINGS") ||
                property == "VS_SETTINGS";
            const bool isCompileOption =
                property == "COMPILE_OPTIONS" ||
                property == "INTERFACE_COMPILE_OPTIONS" ||
                property == "COMPILE_FLAGS" ||
                property == "CMAKE_CXX_FLAGS" ||
                property == "CMAKE_CXX_FLAGS_DEBUG" ||
                property == "CMAKE_CXX_FLAGS_DEVELOPMENT" ||
                property == "CMAKE_CXX_FLAGS_RELEASE" ||
                property == "DIRECTORY_CXX_FLAGS" ||
                property.ends_with(".COMPILE_OPTIONS") ||
                property.ends_with(".COMPILE_FLAGS");

            if (isVisualStudioSetting &&
                normalized.find("forcedincludefiles=") != std::string::npos)
            {
                return true;
            }

            if (isCompileOption)
            {
                constexpr std::string_view shellPrefix = "shell:";
                auto options = std::string_view(normalized);

                while (!options.empty())
                {
                    const auto separator = options.find(';');
                    auto option = trim_ascii(options.substr(0U, separator));

                    if (option.substr(0U, shellPrefix.size()) == shellPrefix)
                    {
                        return true;
                    }

                    if (!option.empty() && option.front() == '@')
                    {
                        return true;
                    }

                    for (std::size_t index = 0U;
                         index < option.size(); ++index)
                    {
                        const bool isArgumentStart =
                            index == 0U ||
                            std::isspace(static_cast<unsigned char>(
                                option[index - 1U])) != 0;

                        if (!isArgumentStart)
                        {
                            continue;
                        }

                        auto candidate = index;

                        if (option[candidate] == '"' ||
                            option[candidate] == '\'')
                        {
                            ++candidate;
                        }

                        if (candidate < option.size() &&
                            option[candidate] == '@')
                        {
                            return true;
                        }

                        if (option.substr(candidate, 3U) == "/fi" ||
                            option.substr(candidate, 3U) == "-fi" ||
                            option.substr(candidate, 8U) == "-include" ||
                            option.substr(candidate, 8U) == "-imacros")
                        {
                            return true;
                        }
                    }

                    if (separator == std::string_view::npos)
                    {
                        break;
                    }

                    options = options.substr(separator + 1U);
                }
            }
        }

        if (lineEnd == std::string_view::npos)
        {
            break;
        }

        lineStart = lineEnd + 1U;
    }

    return false;
}

/// @brief Build Directoryを暗黙IncludeするCMake設定が有効な場合にtrueを返す
[[nodiscard]] bool has_implicit_include_directory(
    std::string_view a_manifest)
{
    constexpr std::array<std::string_view, 2> properties = {
        "CMAKE_INCLUDE_CURRENT_DIR=",
        "CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE=",
    };

    for (const auto property : properties)
    {
        const auto offset = a_manifest.find(property);

        if (offset == std::string_view::npos)
        {
            continue;
        }

        const auto valueStart = offset + property.size();
        const auto valueEnd = a_manifest.find('\n', valueStart);
        const auto value = lower_ascii(trim_ascii(a_manifest.substr(
            valueStart,
            valueEnd == std::string_view::npos
                ? a_manifest.size() - valueStart
                : valueEnd - valueStart)));

        const bool isFalseConstant =
            value.empty() || value == "0" || value == "off" ||
            value == "false" || value == "no" || value == "n" ||
            value == "ignore" || value == "notfound" ||
            value.ends_with("-notfound");

        if (!isFalseConstant)
        {
            return true;
        }
    }

    return false;
}

/// @brief Repository走査外の明示Include Directoryがある場合にtrueを返す
[[nodiscard]] bool has_unaudited_include_directory(
    std::string_view a_manifest,
    const std::filesystem::path &a_repositoryRoot)
{
    std::size_t lineStart = 0U;

    while (lineStart < a_manifest.size())
    {
        const auto lineEnd = a_manifest.find('\n', lineStart);
        const auto line = trim_ascii(a_manifest.substr(
            lineStart,
            lineEnd == std::string_view::npos
                ? a_manifest.size() - lineStart
                : lineEnd - lineStart));
        const auto assignment = line.find('=');
        const auto property = line.substr(0U, assignment);
        const bool isIncludeDirectoryProperty =
            property == "INCLUDE_DIRECTORIES" ||
            property == "INTERFACE_INCLUDE_DIRECTORIES" ||
            (property.substr(0U, 7U) == "SOURCE[" &&
             property.size() >= 21U &&
             property.substr(property.size() - 21U) ==
                 "].INCLUDE_DIRECTORIES");

        if (assignment != std::string_view::npos &&
            isIncludeDirectoryProperty)
        {
            auto directories = line.substr(assignment + 1U);

            while (!directories.empty())
            {
                const auto separator = directories.find(';');
                const auto directory = trim_ascii(
                    directories.substr(0U, separator));

                if (!directory.empty())
                {
                    std::error_code canonicalError;
                    const auto canonicalDirectory =
                        std::filesystem::weakly_canonical(
                            std::filesystem::path(directory), canonicalError);
                    std::error_code relativeError;
                    const auto relativeDirectory = std::filesystem::relative(
                        canonicalDirectory, a_repositoryRoot, relativeError);

                    if (canonicalError || relativeError ||
                        !std::filesystem::path(directory).is_absolute() ||
                        relativeDirectory.empty() ||
                        relativeDirectory.begin() == relativeDirectory.end() ||
                        *relativeDirectory.begin() == ".." ||
                        is_excluded_root_directory(relativeDirectory))
                    {
                        return true;
                    }
                }

                if (separator == std::string_view::npos)
                {
                    break;
                }

                directories = directories.substr(separator + 1U);
            }
        }

        if (lineEnd == std::string_view::npos)
        {
            break;
        }

        lineStart = lineEnd + 1U;
    }

    return false;
}

/// @brief Cue.Mathへ任意Libraryを注入できるLink Propertyが非空の場合にtrueを返す
[[nodiscard]] bool has_link_injection_option(
    std::string_view a_manifest)
{
    constexpr std::string_view privateLinkOptions = "LINK_OPTIONS=";
    constexpr std::array<std::string_view, 3> rejectedProperties = {
        "INTERFACE_LINK_OPTIONS=",
        "LINK_FLAGS=",
        "STATIC_LIBRARY_OPTIONS=",
    };
    std::size_t lineStart = 0U;

    while (lineStart < a_manifest.size())
    {
        const auto lineEnd = a_manifest.find('\n', lineStart);
        const auto line = trim_ascii(a_manifest.substr(
            lineStart,
            lineEnd == std::string_view::npos
                ? a_manifest.size() - lineStart
                : lineEnd - lineStart));

        if (line.substr(0U, privateLinkOptions.size()) == privateLinkOptions)
        {
            auto options = trim_ascii(line.substr(privateLinkOptions.size()));

            while (!options.empty())
            {
                const auto separator = options.find(';');
                const auto option = trim_ascii(options.substr(0U, separator));

                if (!option.empty() && lower_ascii(option) != "/debug")
                {
                    return true;
                }

                if (separator == std::string_view::npos)
                {
                    break;
                }

                options = trim_ascii(options.substr(separator + 1U));
            }
        }

        for (const auto property : rejectedProperties)
        {
            if (line.substr(0U, property.size()) == property &&
                !trim_ascii(line.substr(property.size())).empty())
            {
                return true;
            }
        }

        if (lineEnd == std::string_view::npos)
        {
            break;
        }

        lineStart = lineEnd + 1U;
    }

    return false;
}

/// @brief 展開済みCue.Math Build Propertyが禁止依存を含む場合にtrueを返す
[[nodiscard]] bool has_forbidden_math_build_configuration(
    std::string_view a_manifest)
{
    const auto normalized = lower_ascii(a_manifest);
    constexpr std::array<std::string_view, 15> forbiddenDependencies = {
        "directxmath",
        "directxpackedvector",
        "directxcollision",
        "directxcolors",
        "windows.h",
        "d3d12.h",
        "intrin.h",
        "cue/platform/",
        "cue/rhi/",
        "cue/runtimehost/",
        "cue/editor/",
        "cue.platform",
        "cue.rhi",
        "cue.runtimehost",
        "cue.editor",
    };
    const bool hasForbiddenDependency = std::any_of(
        forbiddenDependencies.begin(), forbiddenDependencies.end(),
        [&normalized](std::string_view a_header)
        {
            return normalized.find(a_header) != std::string::npos;
        });
    const bool hasDirectXDefinition =
        contains_ascii_identifier(a_manifest, "DirectX");
    return hasForbiddenDependency || hasDirectXDefinition ||
           has_forced_include_option(a_manifest) ||
           has_link_injection_option(a_manifest) ||
           has_implicit_include_directory(a_manifest);
}

/// @brief CMakeが生成したCue.Math Build Property Manifestを検証する
[[nodiscard]] bool validate_math_build_manifest(
    const std::filesystem::path &a_path,
    const std::filesystem::path &a_repositoryRoot)
{
    std::ifstream stream(a_path, std::ios::binary);

    if (!stream)
    {
        std::cerr << "Could not read Cue.Math build manifest: "
                  << a_path.string()
                  << '\n';
        return false;
    }

    const std::string source{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
    if (!has_forbidden_math_build_configuration(source) &&
        !has_unaudited_include_directory(source, a_repositoryRoot))
    {
        return true;
    }

    std::cerr << "Forbidden Cue.Math build property dependency: "
              << a_path.string() << '\n';
    return false;
}

/// @brief Lexerと依存判定の回帰確認値を実行する
[[nodiscard]] bool run_lexer_self_tests()
{
    const auto allowed = sanitize_cpp_source(
        "// XMASSERT comment\\\n"
        "XMVECTOR continued comment\n"
        "const char *message = \"XMMATRIX string\";\n"
        "const char *raw = R\"tag(DirectX::BoundingBox)tag\";\n"
        "struct XMLParser {};\n");

    if (has_forbidden_directxmath_tokens(tokenize_cpp(allowed)))
    {
        return false;
    }

    const auto multipleRaw = sanitize_cpp_source(
        "const char *a = R\"a(first)a\"; XMVECTOR value; "
        "const char *b = R\"b(second)b\";");

    if (!has_forbidden_directxmath_tokens(tokenize_cpp(multipleRaw)))
    {
        return false;
    }

    const auto quotedInclude = sanitize_cpp_source("#include \"DirectXMath.h\"\n");

    if (validate_include_directives(
            quotedInclude, false, "QuotedIncludeProbe.cpp", false))
    {
        return false;
    }

    const auto macroInclude = sanitize_cpp_source(
        "#define DX_HEADER <DirectXMath.h>\n#include DX_HEADER\n");

    if (validate_include_directives(
            macroInclude, false, "MacroIncludeProbe.cpp", false))
    {
        return false;
    }

    const auto globalAlias = sanitize_cpp_source(
        "namespace dx = ::DirectX; dx::BoundingBox value{};\n");

    if (!has_forbidden_directxmath_tokens(tokenize_cpp(globalAlias)))
    {
        return false;
    }

    const auto globalUsing = sanitize_cpp_source(
        "using namespace ::DirectX; BoundingBox value{};\n");

    if (!has_forbidden_directxmath_tokens(tokenize_cpp(globalUsing)))
    {
        return false;
    }

    const auto parameterizedNamespaceMacroAlias = sanitize_cpp_source(
        "#define ID(x) x\nID(DirectX)::BoundingBox value{};\n");

    if (!has_forbidden_directxmath_tokens(
            tokenize_cpp(parameterizedNamespaceMacroAlias)))
    {
        return false;
    }

    const auto tokenPastedNamespace = sanitize_cpp_source(
        "#define JOIN(a, b) a##b\n"
        "JOIN(Direct, X)::BoundingBox value{};\n");

    if (!has_macro_token_paste(tokenPastedNamespace))
    {
        return false;
    }

    const auto digraphTokenPaste = sanitize_cpp_source(
        "%:define JOIN(a, b) a%:%:b\n");

    if (!has_macro_token_paste(digraphTokenPaste))
    {
        return false;
    }

    const auto digraphInclude =
        sanitize_cpp_source("%:include <DirectXMath.h>\n");

    if (validate_include_directives(
            digraphInclude, false, "DigraphIncludeProbe.cpp", false))
    {
        return false;
    }

    const auto includeAlias = sanitize_cpp_source(
        "#pragma include_alias(\"SafeHeader.h\", \"windows.h\")\n"
        "#include \"SafeHeader.h\"\n");

    if (validate_include_directives(
            includeAlias, true, "IncludeAliasProbe.cpp", false))
    {
        return false;
    }

    const auto extensionIncludeAlias = sanitize_cpp_source(
        "__pragma(include_alias(\"SafeHeader.h\", \"windows.h\"))\n"
        "#include \"SafeHeader.h\"\n");

    if (validate_include_directives(
            extensionIncludeAlias, true,
            "ExtensionIncludeAliasProbe.cpp", false))
    {
        return false;
    }

    const auto relativeModuleInclude = sanitize_cpp_source(
        "#include \"../../Platform/Public/Cue/Platform/Window.h\"\n");

    if (validate_include_directives(
            relativeModuleInclude, true,
            "Engine/Source/Math/Private/RelativeIncludeProbe.cpp", false))
    {
        return false;
    }

    const auto pathHeaderInclude =
        sanitize_cpp_source("#include <../um/DirectXMath.h>\n");

    if (validate_include_directives(
            pathHeaderInclude, false, "PathHeaderProbe.cpp", false))
    {
        return false;
    }

    const auto headerUnitImport =
        sanitize_cpp_source("export import \"DirectXMath.h\";\n");

    if (validate_include_directives(
            headerUnitImport, false, "HeaderUnitProbe.cpp", false))
    {
        return false;
    }

    const auto compactHeaderUnitImport =
        sanitize_cpp_source("import<DirectXMath.h>;\n");

    if (validate_include_directives(
            compactHeaderUnitImport, false, "CompactHeaderUnitProbe.cpp", false))
    {
        return false;
    }

    const auto platformPathInclude =
        sanitize_cpp_source("#include <../um/windows.h>\n");

    if (validate_include_directives(
            platformPathInclude, true,
            "Engine/Source/Math/Private/PlatformPathProbe.cpp", false))
    {
        return false;
    }

    const auto namespaceMacroAlias = sanitize_cpp_source(
        "#define DX ::DirectX\nDX::BoundingBox value{};\n");

    if (validate_include_directives(
            namespaceMacroAlias, false, "NamespaceMacroProbe.cpp", false))
    {
        return false;
    }

    const auto functionNamespaceMacroAlias = sanitize_cpp_source(
        "#define DX() ::DirectX\nDX()::BoundingBox value{};\n");

    if (validate_include_directives(
            functionNamespaceMacroAlias, false,
            "FunctionNamespaceMacroProbe.cpp", false))
    {
        return false;
    }

    const auto macroHeaderUnitImport = sanitize_cpp_source(
        "#define H <DirectXMath.h>\nimport H;\n");

    if (validate_include_directives(
            macroHeaderUnitImport, false, "MacroHeaderUnitProbe.cpp", false))
    {
        return false;
    }

    const auto quotedMacroHeaderUnitImport = sanitize_cpp_source(
        "#define H \"DirectXMath.h\"\nimport H;\n");

    if (validate_include_directives(
            quotedMacroHeaderUnitImport, false,
            "QuotedMacroHeaderUnitProbe.cpp", false))
    {
        return false;
    }

    const auto functionMacroHeaderUnitImport = sanitize_cpp_source(
        "#define H() <DirectXMath.h>\nimport H();\n");

    if (validate_include_directives(
            functionMacroHeaderUnitImport, false,
            "FunctionMacroHeaderUnitProbe.cpp", false))
    {
        return false;
    }

    const auto inactiveImportMacros = sanitize_cpp_source(
        "#define H() <DirectXMath.h>\nimport H;\n"
        "#define M Cue.Math\n#undef M\nimport M;\n");

    if (!validate_include_directives(
            inactiveImportMacros, false, "InactiveImportMacroProbe.cpp", false))
    {
        return false;
    }

    const auto includedFunctionMacroCall =
        sanitize_cpp_source("import H();\n");

    if (validate_include_directives(
            includedFunctionMacroCall, false,
            "IncludedFunctionMacroCallProbe.cpp", false))
    {
        return false;
    }

    const auto includedFunctionMacroName =
        sanitize_cpp_source("import H;\n");

    if (!validate_include_directives(
            includedFunctionMacroName, false,
            "IncludedFunctionMacroNameProbe.cpp", false))
    {
        return false;
    }

    const auto parameterizedFunctionMacroCall = sanitize_cpp_source(
        "#define H(x) x\nimport H(<DirectXMath.h>);\n");

    if (validate_include_directives(
            parameterizedFunctionMacroCall, false,
            "ParameterizedFunctionMacroCallProbe.cpp", false))
    {
        return false;
    }

    constexpr std::string_view cmakePrecompiledHeader =
        "PRECOMPILE_HEADERS=<DirectXCollision.h>\n";

    if (!has_forbidden_math_build_configuration(cmakePrecompiledHeader))
    {
        return false;
    }

    constexpr std::string_view cmakeCompileDefinition =
        "COMPILE_DEFINITIONS=NS=DirectX\n";

    if (!has_forbidden_math_build_configuration(cmakeCompileDefinition))
    {
        return false;
    }

    constexpr std::array<std::string_view, 3> platformHeaderProperties = {
        "PRECOMPILE_HEADERS=<windows.h>\n",
        "PRECOMPILE_HEADERS=<d3d12.h>\n",
        "PRECOMPILE_HEADERS=<intrin.h>\n",
    };

    for (const auto property : platformHeaderProperties)
    {
        if (!has_forbidden_math_build_configuration(property))
        {
            return false;
        }
    }

    constexpr std::string_view sourceCompileOption =
        "SOURCE[Private/Vector.cpp].COMPILE_OPTIONS=/FI:DirectXCollision.h\n";

    if (!has_forbidden_math_build_configuration(sourceCompileOption))
    {
        return false;
    }

    constexpr std::string_view safeNamedForcedInclude =
        "COMPILE_OPTIONS=/FI:Injected.h\n";

    if (!has_forbidden_math_build_configuration(safeNamedForcedInclude))
    {
        return false;
    }

    constexpr std::string_view dashForcedInclude =
        "COMPILE_OPTIONS=-FIInjected.h\n";

    if (!has_forbidden_math_build_configuration(dashForcedInclude))
    {
        return false;
    }

    constexpr std::string_view shellForcedInclude =
        "COMPILE_OPTIONS=SHELL:/FIInjected.h\n";

    if (!has_forbidden_math_build_configuration(shellForcedInclude))
    {
        return false;
    }

    constexpr std::string_view quotedShellForcedInclude =
        "COMPILE_OPTIONS=SHELL:\"-include\" Injected.h\n";

    if (!has_forbidden_math_build_configuration(quotedShellForcedInclude))
    {
        return false;
    }

    constexpr std::string_view responseFileCompileOption =
        "COMPILE_OPTIONS=@SafeOptions.rsp\n";

    if (!has_forbidden_math_build_configuration(responseFileCompileOption))
    {
        return false;
    }

    constexpr std::string_view rawFlagResponseFile =
        "COMPILE_FLAGS=/W4 @SafeOptions.rsp\n";

    if (!has_forbidden_math_build_configuration(rawFlagResponseFile))
    {
        return false;
    }

    constexpr std::string_view quotedRawFlagResponseFile =
        "COMPILE_FLAGS=/W4 \"@SafeOptions.rsp\"\n";

    if (!has_forbidden_math_build_configuration(quotedRawFlagResponseFile))
    {
        return false;
    }

    constexpr std::string_view quotedDefinitionValue =
        "COMPILE_FLAGS=/DVALUE=\\\"@Safe\\\"\n";

    if (has_forbidden_math_build_configuration(quotedDefinitionValue))
    {
        return false;
    }

    constexpr std::string_view driveIncludeDirectory =
        "INCLUDE_DIRECTORIES=C:/FirstParty/include\n";

    if (has_forced_include_option(driveIncludeDirectory))
    {
        return false;
    }

    constexpr std::string_view definitionWithShellPath =
        "COMPILE_DEFINITIONS=PATH=SHELL:/FirstParty/include\n";

    if (has_forced_include_option(definitionWithShellPath))
    {
        return false;
    }

    constexpr std::string_view compileOptionWithEmbeddedShellPath =
        "COMPILE_OPTIONS=/DPATH=SHELL:/FirstParty/include\n";

    if (has_forced_include_option(compileOptionWithEmbeddedShellPath))
    {
        return false;
    }

    constexpr std::string_view implicitBuildInclude =
        "CMAKE_INCLUDE_CURRENT_DIR=ON\n";

    if (!has_forbidden_math_build_configuration(implicitBuildInclude))
    {
        return false;
    }

    constexpr std::array<std::string_view, 4> falseCMakeValues = {
        "CMAKE_INCLUDE_CURRENT_DIR=N\n",
        "CMAKE_INCLUDE_CURRENT_DIR=IGNORE\n",
        "CMAKE_INCLUDE_CURRENT_DIR=NOTFOUND\n",
        "CMAKE_INCLUDE_CURRENT_DIR=VALUE-NOTFOUND\n",
    };

    for (const auto value : falseCMakeValues)
    {
        if (has_forbidden_math_build_configuration(value))
        {
            return false;
        }
    }

    constexpr std::string_view generatedIncludeDirectory =
        "INCLUDE_DIRECTORIES=C:/CueRepo/out/GeneratedInclude\n";

    if (!has_unaudited_include_directory(
            generatedIncludeDirectory, "C:/CueRepo"))
    {
        return false;
    }

    constexpr std::string_view interfaceLinkOption =
        "INTERFACE_LINK_OPTIONS=/DEFAULTLIB:SafeLibrary.lib\n";

    if (!has_forbidden_math_build_configuration(interfaceLinkOption))
    {
        return false;
    }

    constexpr std::string_view targetCompileFlag =
        "COMPILE_FLAGS=/FI:DirectXCollision.h\n";

    if (!has_forbidden_math_build_configuration(targetCompileFlag))
    {
        return false;
    }

    constexpr std::string_view sourceVisualStudioSetting =
        "SOURCE[Private/Vector.cpp].VS_SETTINGS="
        "ForcedIncludeFiles=DirectXCollision.h\n";

    if (!has_forbidden_math_build_configuration(sourceVisualStudioSetting))
    {
        return false;
    }

    constexpr std::string_view allowedBuildManifest =
        "COMPILE_DEFINITIONS=CUE_BUILD_DEBUG=1\n"
        "LINK_LIBRARIES=Cue.Foundation\n";

    if (has_forbidden_math_build_configuration(allowedBuildManifest))
    {
        return false;
    }

    return true;
}

/// @brief Repository全体を列挙してMath依存規則を検証する
[[nodiscard]] bool validate_repository(
    const std::filesystem::path &a_repositoryRoot)
{
    std::error_code iteratorError;
    std::filesystem::recursive_directory_iterator iterator(
        a_repositoryRoot,
        std::filesystem::directory_options::skip_permission_denied,
        iteratorError);
    const std::filesystem::recursive_directory_iterator end;

    if (iteratorError)
    {
        std::cerr << "Could not enumerate repository: "
                  << iteratorError.message() << '\n';
        return false;
    }

    while (iterator != end)
    {
        const auto path = iterator->path();
        std::error_code relativeError;
        const auto relativePath =
            std::filesystem::relative(path, a_repositoryRoot, relativeError);

        if (relativeError)
        {
            std::cerr << "Could not resolve repository entry: "
                      << path.string() << '\n';
            return false;
        }

        std::error_code typeError;

        if (iterator->is_directory(typeError) &&
            is_excluded_root_directory(relativePath))
        {
            iterator.disable_recursion_pending();
        }
        else if (!typeError && iterator->is_regular_file(typeError) &&
                 !typeError && is_cpp_source_path(path) &&
                 !validate_cpp_file(a_repositoryRoot, path))
        {
            return false;
        }
        iterator.increment(iteratorError);

        if (iteratorError)
        {
            std::cerr << "Could not continue repository enumeration: "
                      << iteratorError.message() << '\n';
            return false;
        }
    }

    return true;
}
} // namespace

/// @brief Math依存Gateの自己TestとRepository全体検査を実行する
int main(int a_argumentCount, char **a_arguments)
{
    if (a_argumentCount != 3 || a_arguments[1] == nullptr ||
        a_arguments[2] == nullptr)
    {
        std::cerr << "Repository root and build manifest are required\n";
        return 2;
    }

    if (!run_lexer_self_tests())
    {
        std::cerr << "Math dependency lexer self-test failed\n";
        return 3;
    }

    std::error_code rootError;
    const auto repositoryRoot =
        std::filesystem::weakly_canonical(a_arguments[1], rootError);

    if (rootError || !std::filesystem::is_directory(repositoryRoot))
    {
        std::cerr << "Repository root is invalid\n";
        return 4;
    }

    if (!validate_math_build_manifest(a_arguments[2], repositoryRoot))
    {
        return 5;
    }

    return validate_repository(repositoryRoot) ? 0 : 1;
}
