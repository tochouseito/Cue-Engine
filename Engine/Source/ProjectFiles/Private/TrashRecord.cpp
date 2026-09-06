#include "TrashRecord.h"

#include <Cue/Foundation/Assert.h>
#include <Cue/IO/RelativePath.h>
#include <Cue/Project/Descriptor.h>
#include <Cue/ProjectFiles/Error.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr cue::project_files_private::TrashRecordParseLimits k_hardLimits{
    16U * 1024U * 1024U, 8U, 4096U, 32768U, 16U, 262144U};

/// @brief noexcept境界でのAllocation失敗をProject Fatal Policyへ渡す
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate("Trash record allocation failed");
    std::abort();
}

/// @brief Trash Recordの不正入力を一貫したProject File Errorへ変換する
[[nodiscard]] cue::Error make_record_error(const cue::AssertContext &a_assertContext,
                                           std::string_view a_message) noexcept
{
    return cue::project_files::make_project_file_error(
        a_assertContext, cue::project_files::ProjectFileError::RecoveryRequired, a_message);
}

enum class JsonKind : std::uint8_t
{
    Object,
    Array,
    String,
    Number
};

struct JsonValue final
{
    JsonKind kind = JsonKind::String;
    std::string text;
    std::vector<JsonValue> elements;
    std::vector<std::pair<std::string, JsonValue>> members;
};

class JsonParser final
{
  public:
    /// @brief 入力とResource上限を借用してParserを初期化する
    JsonParser(std::string_view a_input, cue::project_files_private::TrashRecordParseLimits a_limits) noexcept
        : m_input(a_input), m_limits(a_limits)
    {
    }

    /// @brief Root JSON Valueを一つだけParseする
    [[nodiscard]] bool parse(JsonValue &a_output) noexcept
    {
        skip_whitespace();
        if (!parse_value(a_output, 1U))
        {
            return false;
        }
        skip_whitespace();
        return m_offset == m_input.size();
    }

  private:
    /// @brief JSONで許可されたWhitespaceを読み飛ばす
    void skip_whitespace() noexcept
    {
        while (m_offset < m_input.size() && (m_input[m_offset] == ' ' || m_input[m_offset] == '\t' ||
                                             m_input[m_offset] == '\r' || m_input[m_offset] == '\n'))
        {
            ++m_offset;
        }
    }

    /// @brief 次のTokenをResource上限内のValueへ変換する
    [[nodiscard]] bool parse_value(JsonValue &a_output, std::size_t a_depth) noexcept
    {
        if (a_depth > m_limits.maxDepth || m_values == m_limits.maxValues || m_offset >= m_input.size())
        {
            return false;
        }
        ++m_values;
        const char current = m_input[m_offset];
        if (current == '{')
        {
            return parse_object(a_output, a_depth);
        }
        if (current == '[')
        {
            return parse_array(a_output, a_depth);
        }
        if (current == '"')
        {
            a_output.kind = JsonKind::String;
            return parse_string(a_output.text);
        }
        if (current >= '0' && current <= '9')
        {
            return parse_number(a_output);
        }
        return false;
    }

    /// @brief JSON ObjectのMemberを上限付きでParseする
    [[nodiscard]] bool parse_object(JsonValue &a_output, std::size_t a_depth) noexcept
    {
        a_output.kind = JsonKind::Object;
        ++m_offset;
        skip_whitespace();
        if (m_offset < m_input.size() && m_input[m_offset] == '}')
        {
            ++m_offset;
            return true;
        }
        while (m_offset < m_input.size())
        {
            if (a_output.members.size() == m_limits.maxObjectMembers)
            {
                return false;
            }
            std::string name;
            if (!parse_string(name))
            {
                return false;
            }
            skip_whitespace();
            if (m_offset >= m_input.size() || m_input[m_offset] != ':')
            {
                return false;
            }
            ++m_offset;
            skip_whitespace();
            JsonValue value;
            if (!parse_value(value, a_depth + 1U))
            {
                return false;
            }
            try
            {
                a_output.members.emplace_back(std::move(name), std::move(value));
            }
            catch (...)
            {
                return false;
            }
            skip_whitespace();
            if (m_offset < m_input.size() && m_input[m_offset] == '}')
            {
                ++m_offset;
                return true;
            }
            if (m_offset >= m_input.size() || m_input[m_offset] != ',')
            {
                return false;
            }
            ++m_offset;
            skip_whitespace();
        }
        return false;
    }

    /// @brief JSON ArrayのElementを上限付きでParseする
    [[nodiscard]] bool parse_array(JsonValue &a_output, std::size_t a_depth) noexcept
    {
        a_output.kind = JsonKind::Array;
        ++m_offset;
        skip_whitespace();
        if (m_offset < m_input.size() && m_input[m_offset] == ']')
        {
            ++m_offset;
            return true;
        }
        while (m_offset < m_input.size())
        {
            if (a_output.elements.size() == m_limits.maxArrayElements)
            {
                return false;
            }
            JsonValue value;
            if (!parse_value(value, a_depth + 1U))
            {
                return false;
            }
            try
            {
                a_output.elements.push_back(std::move(value));
            }
            catch (...)
            {
                return false;
            }
            skip_whitespace();
            if (m_offset < m_input.size() && m_input[m_offset] == ']')
            {
                ++m_offset;
                return true;
            }
            if (m_offset >= m_input.size() || m_input[m_offset] != ',')
            {
                return false;
            }
            ++m_offset;
            skip_whitespace();
        }
        return false;
    }

    /// @brief JSON StringをASCII範囲へDecodeする
    [[nodiscard]] bool parse_string(std::string &a_output) noexcept
    {
        if (m_offset >= m_input.size() || m_input[m_offset] != '"')
        {
            return false;
        }
        ++m_offset;
        try
        {
            while (m_offset < m_input.size())
            {
                const unsigned char current = static_cast<unsigned char>(m_input[m_offset++]);
                if (current == '"')
                {
                    return a_output.size() <= m_limits.maxStringBytes;
                }
                if (current < 0x20U || current >= 0x80U)
                {
                    return false;
                }
                if (current != '\\')
                {
                    a_output.push_back(static_cast<char>(current));
                }
                else
                {
                    if (m_offset >= m_input.size())
                    {
                        return false;
                    }
                    const char escaped = m_input[m_offset++];
                    switch (escaped)
                    {
                    case '"':
                    case '\\':
                    case '/':
                        a_output.push_back(escaped);
                        break;
                    case 'b':
                        a_output.push_back('\b');
                        break;
                    case 'f':
                        a_output.push_back('\f');
                        break;
                    case 'n':
                        a_output.push_back('\n');
                        break;
                    case 'r':
                        a_output.push_back('\r');
                        break;
                    case 't':
                        a_output.push_back('\t');
                        break;
                    case 'u':
                    {
                        if (m_input.size() - m_offset < 4U)
                        {
                            return false;
                        }
                        unsigned int value = 0U;
                        for (std::size_t index = 0U; index < 4U; ++index)
                        {
                            const char digit = m_input[m_offset++];
                            value *= 16U;
                            if (digit >= '0' && digit <= '9')
                            {
                                value += static_cast<unsigned int>(digit - '0');
                            }
                            else if (digit >= 'a' && digit <= 'f')
                            {
                                value += static_cast<unsigned int>(digit - 'a' + 10);
                            }
                            else if (digit >= 'A' && digit <= 'F')
                            {
                                value += static_cast<unsigned int>(digit - 'A' + 10);
                            }
                            else
                            {
                                return false;
                            }
                        }
                        if (value < 0x20U || value >= 0x80U)
                        {
                            return false;
                        }
                        a_output.push_back(static_cast<char>(value));
                        break;
                    }
                    default:
                        return false;
                    }
                }
                if (a_output.size() > m_limits.maxStringBytes)
                {
                    return false;
                }
            }
        }
        catch (...)
        {
            return false;
        }
        return false;
    }

    /// @brief 符号なし整数形式だけをNumber Tokenとして保持する
    [[nodiscard]] bool parse_number(JsonValue &a_output) noexcept
    {
        const std::size_t begin = m_offset;
        while (m_offset < m_input.size() && m_input[m_offset] >= '0' && m_input[m_offset] <= '9')
        {
            ++m_offset;
        }
        if (m_offset == begin || (m_offset - begin > 1U && m_input[begin] == '0'))
        {
            return false;
        }
        a_output.kind = JsonKind::Number;
        try
        {
            a_output.text.assign(m_input.substr(begin, m_offset - begin));
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    std::string_view m_input;
    cue::project_files_private::TrashRecordParseLimits m_limits;
    std::size_t m_offset = 0U;
    std::size_t m_values = 0U;
};

/// @brief Objectから重複しないMemberを取得する
[[nodiscard]] const JsonValue *find_unique_member(const JsonValue &a_object, std::string_view a_name) noexcept
{
    const JsonValue *found = nullptr;
    for (const auto &[name, value] : a_object.members)
    {
        if (name == a_name)
        {
            if (found != nullptr)
            {
                return nullptr;
            }
            found = &value;
        }
    }
    return found;
}

/// @brief Objectが未知または重複Memberを含まず指定集合と一致するか判定する
template <std::size_t Size>
[[nodiscard]] bool has_exact_members(const JsonValue &a_object,
                                     const std::array<std::string_view, Size> &a_names) noexcept
{
    if (a_object.kind != JsonKind::Object || a_object.members.size() != Size)
    {
        return false;
    }
    for (std::size_t index = 0U; index < a_object.members.size(); ++index)
    {
        if (std::find(a_names.begin(), a_names.end(), a_object.members[index].first) == a_names.end())
        {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous)
        {
            if (a_object.members[previous].first == a_object.members[index].first)
            {
                return false;
            }
        }
    }
    return true;
}

/// @brief JSON StringのCanonical uint64表現を変換する
[[nodiscard]] bool parse_u64_string(const JsonValue &a_value, std::uint64_t &a_output) noexcept
{
    if (a_value.kind != JsonKind::String || a_value.text.empty() ||
        (a_value.text.size() > 1U && a_value.text.front() == '0'))
    {
        return false;
    }
    const auto [end, error] = std::from_chars(a_value.text.data(), a_value.text.data() + a_value.text.size(), a_output);
    return error == std::errc{} && end == a_value.text.data() + a_value.text.size();
}

/// @brief 正確に16桁のlowercase hexadecimal Digestを変換する
[[nodiscard]] bool parse_digest(const JsonValue &a_value, std::uint64_t &a_output) noexcept
{
    if (a_value.kind != JsonKind::String || a_value.text.size() != 16U ||
        std::any_of(
            a_value.text.begin(), a_value.text.end(),
            /// @brief Digest文字がlowercase hexadecimal以外か判定する
            [](char a_character) noexcept
            { return !((a_character >= '0' && a_character <= '9') || (a_character >= 'a' && a_character <= 'f')); }))
    {
        return false;
    }
    const auto [end, error] =
        std::from_chars(a_value.text.data(), a_value.text.data() + a_value.text.size(), a_output, 16);
    return error == std::errc{} && end == a_value.text.data() + a_value.text.size();
}

/// @brief lowercase UUID Version 4とRFC VariantをAllocationなしで検証する
[[nodiscard]] bool is_valid_operation_id(std::string_view a_id) noexcept
{
    if (a_id.size() != 36U || a_id[8U] != '-' || a_id[13U] != '-' || a_id[18U] != '-' || a_id[23U] != '-' ||
        a_id[14U] != '4' || (a_id[19U] != '8' && a_id[19U] != '9' && a_id[19U] != 'a' && a_id[19U] != 'b'))
    {
        return false;
    }
    for (std::size_t index = 0U; index < a_id.size(); ++index)
    {
        if (index == 8U || index == 13U || index == 18U || index == 23U)
        {
            continue;
        }
        const char value = a_id[index];
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
        {
            return false;
        }
    }
    return true;
}

/// @brief Portable ASCII大小文字比較、Type、元Spellingの順でManifest Entryを比較する
[[nodiscard]] bool manifest_entry_less(const cue::WorkspaceManifestEntry &a_left,
                                       const cue::WorkspaceManifestEntry &a_right) noexcept
{
    const std::size_t common = std::min(a_left.path.size(), a_right.path.size());
    for (std::size_t index = 0U; index < common; ++index)
    {
        const char left = a_left.path[index] >= 'A' && a_left.path[index] <= 'Z'
                              ? static_cast<char>(a_left.path[index] + ('a' - 'A'))
                              : a_left.path[index];
        const char right = a_right.path[index] >= 'A' && a_right.path[index] <= 'Z'
                               ? static_cast<char>(a_right.path[index] + ('a' - 'A'))
                               : a_right.path[index];
        if (left != right)
        {
            return left < right;
        }
    }
    if (a_left.path.size() != a_right.path.size())
    {
        return a_left.path.size() < a_right.path.size();
    }
    if (a_left.type != a_right.type)
    {
        return a_left.type < a_right.type;
    }
    return a_left.path < a_right.path;
}

/// @brief JSONへASCII StringをEscapeして追加する
void append_json_string(std::string &a_output, std::string_view a_text)
{
    a_output.push_back('"');
    for (const unsigned char value : a_text)
    {
        switch (value)
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
            a_output.push_back(static_cast<char>(value));
            break;
        }
    }
    a_output.push_back('"');
}

/// @brief uint64値をCanonical decimal JSON Stringへ追加する
void append_u64_string(std::string &a_output, std::uint64_t a_value, const cue::AssertContext &a_assertContext) noexcept
{
    std::array<char, 32U> text{};
    const auto [end, error] = std::to_chars(text.data(), text.data() + text.size(), a_value);
    if (error != std::errc{})
    {
        a_assertContext.fatal_handler().terminate("Trash record integer formatting failed");
        std::abort();
    }
    append_json_string(a_output, std::string_view(text.data(), static_cast<std::size_t>(end - text.data())));
}

/// @brief uint64値を16桁lowercase hexadecimal JSON Stringへ追加する
void append_digest(std::string &a_output, std::uint64_t a_value)
{
    constexpr std::array<char, 16U> k_hex{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::array<char, 16U> text{};
    for (std::size_t index = 0U; index < text.size(); ++index)
    {
        const std::size_t shift = (text.size() - index - 1U) * 4U;
        text[index] = k_hex[(a_value >> shift) & 0x0fU];
    }
    append_json_string(a_output, std::string_view(text.data(), text.size()));
}

/// @brief JSON String状態をTrash Record enumへ変換する
[[nodiscard]] std::optional<cue::project_files_private::TrashRecordState> parse_state(const JsonValue &a_value) noexcept
{
    using cue::project_files_private::TrashRecordState;
    if (a_value.kind != JsonKind::String)
    {
        return std::nullopt;
    }
    if (a_value.text == "allocating")
    {
        return TrashRecordState::Allocating;
    }
    if (a_value.text == "prepared")
    {
        return TrashRecordState::Prepared;
    }
    if (a_value.text == "trashed")
    {
        return TrashRecordState::Trashed;
    }
    if (a_value.text == "restoring")
    {
        return TrashRecordState::Restoring;
    }
    if (a_value.text == "restored")
    {
        return TrashRecordState::Restored;
    }
    return std::nullopt;
}
} // namespace

namespace cue::project_files_private
{
bool TrashRecordParseLimits::is_valid() const noexcept
{
    return maxBytes != 0U && maxDepth != 0U && maxStringBytes != 0U && maxArrayElements != 0U &&
           maxObjectMembers != 0U && maxValues != 0U;
}

TrashRecordParseLimits trash_record_hard_limits() noexcept
{
    return k_hardLimits;
}

std::string_view trash_record_state_name(TrashRecordState a_state) noexcept
{
    switch (a_state)
    {
    case TrashRecordState::Allocating:
        return "allocating";
    case TrashRecordState::Prepared:
        return "prepared";
    case TrashRecordState::Trashed:
        return "trashed";
    case TrashRecordState::Restoring:
        return "restoring";
    case TrashRecordState::Restored:
        return "restored";
    }
    return {};
}

Result<std::vector<std::byte>> serialize_trash_record(const TrashRecord &a_record,
                                                      const AssertContext &a_assertContext) noexcept
{
    if (!is_valid_operation_id(a_record.projectId) || !is_valid_operation_id(a_record.operationId) ||
        a_record.originalPath.empty() ||
        (a_record.fingerprint.type != WorkspaceEntryType::RegularFile &&
         a_record.fingerprint.type != WorkspaceEntryType::Directory) ||
        (a_record.fingerprint.type == WorkspaceEntryType::RegularFile && !a_record.fingerprint.file.has_value()))
    {
        return Result<std::vector<std::byte>>::failure(
            make_record_error(a_assertContext, "Trash record is incomplete"));
    }
    if (a_record.projectId.size() > k_hardLimits.maxStringBytes ||
        a_record.operationId.size() > k_hardLimits.maxStringBytes ||
        a_record.originalPath.size() > k_hardLimits.maxStringBytes ||
        a_record.fingerprint.manifest.size() > k_hardLimits.maxArrayElements ||
        std::any_of(a_record.fingerprint.manifest.begin(), a_record.fingerprint.manifest.end(),
                    /// @brief Writer入力がv1単一String Hard Limitを超えるか判定する
                    [](const WorkspaceManifestEntry &a_entry) noexcept
                    { return a_entry.path.size() > k_hardLimits.maxStringBytes; }))
    {
        return Result<std::vector<std::byte>>::failure(
            make_record_error(a_assertContext, "Trash record exceeds the schema version 1 writer limits"));
    }
    Result<RelativePath> original = RelativePath::parse(a_record.originalPath, a_assertContext);
    if (!original)
    {
        return Result<std::vector<std::byte>>::failure(
            reclassify_project_file_error(a_assertContext, project_files::ProjectFileError::RecoveryRequired,
                                          "Trash record original path is invalid", std::move(*original.try_error())));
    }

    std::vector<WorkspaceManifestEntry> orderedManifest;
    std::string json;
    try
    {
        orderedManifest = a_record.fingerprint.manifest;
        std::sort(orderedManifest.begin(), orderedManifest.end(), manifest_entry_less);
        json.reserve(512U + a_record.fingerprint.manifest.size() * 128U);
        json.append("{\"schemaVersion\":1,\"projectId\":");
        append_json_string(json, a_record.projectId);
        json.append(",\"operationId\":");
        append_json_string(json, a_record.operationId);
        json.append(",\"state\":");
        append_json_string(json, trash_record_state_name(a_record.state));
        json.append(",\"originalArea\":\"sourceAssets\",\"originalPath\":");
        append_json_string(json, a_record.originalPath);
        json.append(",\"entryType\":");
        append_json_string(json,
                           a_record.fingerprint.type == WorkspaceEntryType::RegularFile ? "regularFile" : "directory");
        if (a_record.fingerprint.type == WorkspaceEntryType::RegularFile)
        {
            json.append(",\"byteSize\":");
            append_u64_string(json, a_record.fingerprint.file->byteSize, a_assertContext);
            json.append(",\"contentDigest\":");
            append_digest(json, a_record.fingerprint.file->contentDigest);
        }
        else
        {
            json.append(",\"manifest\":[");
            for (std::size_t index = 0U; index < orderedManifest.size(); ++index)
            {
                const WorkspaceManifestEntry &entry = orderedManifest[index];
                if (index != 0U)
                {
                    json.push_back(',');
                }
                json.append("{\"path\":");
                append_json_string(json, entry.path);
                json.append(",\"entryType\":");
                append_json_string(json, entry.type == WorkspaceEntryType::Directory ? "directory" : "regularFile");
                if (entry.type == WorkspaceEntryType::RegularFile && entry.file.has_value())
                {
                    json.append(",\"byteSize\":");
                    append_u64_string(json, entry.file->byteSize, a_assertContext);
                    json.append(",\"contentDigest\":");
                    append_digest(json, entry.file->contentDigest);
                }
                else if (entry.type != WorkspaceEntryType::Directory)
                {
                    return Result<std::vector<std::byte>>::failure(
                        make_record_error(a_assertContext, "Trash record manifest is invalid"));
                }
                json.push_back('}');
            }
            json.push_back(']');
        }
        json.push_back('}');
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    if (json.size() > k_hardLimits.maxBytes)
    {
        return Result<std::vector<std::byte>>::failure(
            make_record_error(a_assertContext, "Trash record exceeds the schema version 1 byte limit"));
    }

    std::vector<std::byte> bytes;
    try
    {
        bytes.resize(json.size());
        std::transform(json.begin(), json.end(), bytes.begin(),
                       /// @brief JSON Characterを同じBit表現のByteへ変換する
                       [](char a_value) noexcept { return static_cast<std::byte>(a_value); });
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    Result<TrashRecord> verified = parse_trash_record(bytes, k_hardLimits, a_assertContext);
    if (!verified)
    {
        return Result<std::vector<std::byte>>::failure(reclassify_project_file_error(
            a_assertContext, project_files::ProjectFileError::RecoveryRequired,
            "Trash record writer produced data outside the schema version 1 limits", std::move(*verified.try_error())));
    }
    return Result<std::vector<std::byte>>::success(std::move(bytes));
}

Result<TrashRecord> parse_trash_record(std::span<const std::byte> a_bytes, TrashRecordParseLimits a_limits,
                                       const AssertContext &a_assertContext) noexcept
{
    if (!a_limits.is_valid() || a_limits.maxBytes > k_hardLimits.maxBytes ||
        a_limits.maxDepth > k_hardLimits.maxDepth || a_limits.maxStringBytes > k_hardLimits.maxStringBytes ||
        a_limits.maxArrayElements > k_hardLimits.maxArrayElements ||
        a_limits.maxObjectMembers > k_hardLimits.maxObjectMembers || a_limits.maxValues > k_hardLimits.maxValues ||
        a_bytes.size() > a_limits.maxBytes)
    {
        return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash record limits are invalid"));
    }
    const std::string_view json(reinterpret_cast<const char *>(a_bytes.data()), a_bytes.size());
    JsonValue root;
    JsonParser parser(json, a_limits);
    if (!parser.parse(root) || root.kind != JsonKind::Object)
    {
        return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash record JSON is invalid"));
    }
    const JsonValue *schema = find_unique_member(root, "schemaVersion");
    const JsonValue *projectId = find_unique_member(root, "projectId");
    const JsonValue *operationId = find_unique_member(root, "operationId");
    const JsonValue *state = find_unique_member(root, "state");
    const JsonValue *area = find_unique_member(root, "originalArea");
    const JsonValue *path = find_unique_member(root, "originalPath");
    const JsonValue *entryType = find_unique_member(root, "entryType");
    if (schema == nullptr || projectId == nullptr || operationId == nullptr || state == nullptr || area == nullptr ||
        path == nullptr || entryType == nullptr || schema->kind != JsonKind::Number || schema->text != "1" ||
        projectId->kind != JsonKind::String || operationId->kind != JsonKind::String ||
        !is_valid_operation_id(projectId->text) || !is_valid_operation_id(operationId->text) ||
        area->kind != JsonKind::String || area->text != "sourceAssets" || path->kind != JsonKind::String ||
        entryType->kind != JsonKind::String)
    {
        return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash record schema is invalid"));
    }
    const std::optional<TrashRecordState> parsedState = parse_state(*state);
    Result<RelativePath> originalPath = RelativePath::parse(path->text, a_assertContext);
    if (!parsedState.has_value() || !originalPath)
    {
        return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash record identity is invalid"));
    }

    TrashRecord record;
    try
    {
        record.projectId = projectId->text;
        record.operationId = operationId->text;
        record.originalPath = path->text;
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    record.state = *parsedState;

    if (entryType->text == "regularFile")
    {
        constexpr std::array<std::string_view, 9U> k_members{"schemaVersion", "projectId",    "operationId",
                                                             "state",         "originalArea", "originalPath",
                                                             "entryType",     "byteSize",     "contentDigest"};
        const JsonValue *size = find_unique_member(root, "byteSize");
        const JsonValue *digest = find_unique_member(root, "contentDigest");
        WorkspaceFileFingerprint file;
        if (!has_exact_members(root, k_members) || size == nullptr || digest == nullptr ||
            !parse_u64_string(*size, file.byteSize) || !parse_digest(*digest, file.contentDigest))
        {
            return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash file record is invalid"));
        }
        record.fingerprint.type = WorkspaceEntryType::RegularFile;
        record.fingerprint.file = file;
        return Result<TrashRecord>::success(std::move(record));
    }
    if (entryType->text != "directory")
    {
        return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash record entry type is invalid"));
    }
    constexpr std::array<std::string_view, 8U> k_members{"schemaVersion", "projectId",    "operationId", "state",
                                                         "originalArea",  "originalPath", "entryType",   "manifest"};
    const JsonValue *manifest = find_unique_member(root, "manifest");
    if (!has_exact_members(root, k_members) || manifest == nullptr || manifest->kind != JsonKind::Array)
    {
        return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash directory record is invalid"));
    }
    record.fingerprint.type = WorkspaceEntryType::Directory;
    std::vector<std::pair<std::string, WorkspaceEntryType>> parents;
    try
    {
        record.fingerprint.manifest.reserve(manifest->elements.size());
        parents.reserve(manifest->elements.size());
    }
    catch (...)
    {
        terminate_allocation(a_assertContext);
    }
    for (const JsonValue &value : manifest->elements)
    {
        const JsonValue *manifestPath = find_unique_member(value, "path");
        const JsonValue *manifestType = find_unique_member(value, "entryType");
        if (manifestPath == nullptr || manifestType == nullptr || manifestPath->kind != JsonKind::String ||
            manifestType->kind != JsonKind::String)
        {
            return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash manifest entry is invalid"));
        }
        Result<RelativePath> relative = RelativePath::parse(manifestPath->text, a_assertContext);
        if (!relative)
        {
            return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash manifest path is invalid"));
        }
        WorkspaceManifestEntry entry;
        try
        {
            entry.path = manifestPath->text;
        }
        catch (...)
        {
            terminate_allocation(a_assertContext);
        }
        if (manifestType->text == "directory")
        {
            constexpr std::array<std::string_view, 2U> k_directoryMembers{"path", "entryType"};
            if (!has_exact_members(value, k_directoryMembers))
            {
                return Result<TrashRecord>::failure(
                    make_record_error(a_assertContext, "Trash directory manifest entry is invalid"));
            }
            entry.type = WorkspaceEntryType::Directory;
        }
        else if (manifestType->text == "regularFile")
        {
            constexpr std::array<std::string_view, 4U> k_fileMembers{"path", "entryType", "byteSize", "contentDigest"};
            const JsonValue *size = find_unique_member(value, "byteSize");
            const JsonValue *digest = find_unique_member(value, "contentDigest");
            WorkspaceFileFingerprint file;
            if (!has_exact_members(value, k_fileMembers) || size == nullptr || digest == nullptr ||
                !parse_u64_string(*size, file.byteSize) || !parse_digest(*digest, file.contentDigest))
            {
                return Result<TrashRecord>::failure(
                    make_record_error(a_assertContext, "Trash file manifest entry is invalid"));
            }
            entry.type = WorkspaceEntryType::RegularFile;
            entry.file = file;
        }
        else
        {
            return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash manifest type is invalid"));
        }
        const std::string key = relative.try_value()->comparison_key(a_assertContext);
        if (std::any_of(parents.begin(), parents.end(),
                        /// @brief Portable比較で同一となる既存Manifest Pathを検出する
                        [&](const auto &a_existing) noexcept { return a_existing.first == key; }))
        {
            return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash manifest path collides"));
        }
        try
        {
            parents.emplace_back(key, entry.type);
            record.fingerprint.manifest.push_back(std::move(entry));
        }
        catch (...)
        {
            terminate_allocation(a_assertContext);
        }
    }
    for (const WorkspaceManifestEntry &entry : record.fingerprint.manifest)
    {
        const std::size_t separator = entry.path.rfind('/');
        if (separator == std::string::npos)
        {
            continue;
        }
        Result<RelativePath> parentPath =
            RelativePath::parse(std::string_view(entry.path).substr(0U, separator), a_assertContext);
        const std::string parentKey = parentPath.try_value()->comparison_key(a_assertContext);
        if (std::none_of(
                parents.begin(), parents.end(),
                /// @brief Manifest Entryの親Directoryが既に記録済みか判定する
                [&](const auto &a_existing) noexcept
                { return a_existing.first == parentKey && a_existing.second == WorkspaceEntryType::Directory; }))
        {
            return Result<TrashRecord>::failure(make_record_error(a_assertContext, "Trash manifest parent is missing"));
        }
    }
    std::sort(record.fingerprint.manifest.begin(), record.fingerprint.manifest.end(), manifest_entry_less);
    return Result<TrashRecord>::success(std::move(record));
}
} // namespace cue::project_files_private
