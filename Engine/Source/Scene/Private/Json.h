#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cue::scene_private
{
/// @brief Scene Reader内部で所有するJSON Value種別
enum class JsonType : std::uint8_t
{
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

/// @brief 復号値と元Text範囲を所有するScene内部JSON Tree
struct JsonValue final
{
    JsonType type = JsonType::Null;
    bool boolean = false;
    std::string text;
    std::vector<JsonValue> elements;
    std::vector<std::pair<std::string, JsonValue>> members;
    std::size_t begin = 0U;
    std::size_t end = 0U;
};

/// @brief UTF-8 JSON文書全体を重複Memberなしの所有Treeへ解析する
[[nodiscard]] bool parse_json_document(std::string_view a_input, JsonValue &a_value, std::string_view &a_error);
/// @brief JSON Objectから名前一致Memberを検索する
[[nodiscard]] const JsonValue *find_json_member(const JsonValue &a_object, std::string_view a_name) noexcept;
/// @brief UTF-8 TextをJSON Stringとして末尾へ追加する
void append_json_string(std::string &a_output, std::string_view a_text);
/// @brief JSON Treeを固定compact表現として末尾へ追加する
void append_json_value(std::string &a_output, const JsonValue &a_value);
} // namespace cue::scene_private
