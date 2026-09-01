#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cue::project_private
{
/// @brief Project Module 内で共有する JSON Value の種類
enum class JsonType : std::uint8_t
{
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

/// @brief Descriptor と Workspace が同じ Reader から受け取る所有 JSON Tree
struct JsonValue final
{
    JsonType type = JsonType::Null;
    bool boolean = false;
    std::string text;
    std::vector<JsonValue> elements;
    std::vector<std::pair<std::string, JsonValue>> members;
};

/// @brief Project Module 共通の上限付き Reader で JSON 文書全体を解析する
[[nodiscard]] bool parse_json_document(std::string_view a_input, JsonValue &a_value, std::string_view &a_error);

/// @brief Object から指定名の Member を非所有で検索する
[[nodiscard]] const JsonValue *find_json_member(const JsonValue &a_object, std::string_view a_name) noexcept;

/// @brief Object が指定された Member 名だけを一度ずつ持つか検証する
template <std::size_t Size>
[[nodiscard]] bool has_exact_json_members(const JsonValue &a_object,
                                          const std::array<std::string_view, Size> &a_names) noexcept
{
    if (a_object.type != JsonType::Object || a_object.members.size() != a_names.size())
    {
        return false;
    }
    for (const std::string_view name : a_names)
    {
        if (find_json_member(a_object, name) == nullptr)
        {
            return false;
        }
    }
    return true;
}

/// @brief UTF-8 Text を JSON String として必要な Character だけ Escape して追加する
void write_json_string(std::string &a_output, std::string_view a_text);

/// @brief 所有 Text が UTF-8、非 Control、Byte 上限の Workspace 規則を満たすか検証する
[[nodiscard]] bool is_valid_json_text(std::string_view a_text, std::size_t a_maximumBytes) noexcept;
} // namespace cue::project_private
