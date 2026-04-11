#pragma once

namespace Cue::Editor::Icon
{
    // ImGui は Material Symbols の ligature を解釈しないため、
    // 文字列名ではなく code point 文字列をここへ固定する。
    inline constexpr const char* k_add = u8"\uE145";
    inline constexpr const char* k_bugReport = u8"\uE868";
    inline constexpr const char* k_chevronRight = u8"\uE5CC";
    inline constexpr const char* k_delete = u8"\uE872";
    inline constexpr const char* k_folder = u8"\uE2C7";
    inline constexpr const char* k_home = u8"\uE88A";
    inline constexpr const char* k_info = u8"\uE88E";
    inline constexpr const char* k_menu = u8"\uE5D2";
    inline constexpr const char* k_moreVert = u8"\uE5D4";
    inline constexpr const char* k_playArrow = u8"\uE037";
    inline constexpr const char* k_search = u8"\uE8B6";
    inline constexpr const char* k_settings = u8"\uE8B8";
    inline constexpr const char* k_stop = u8"\uE047";
    inline constexpr const char* k_viewColumn = u8"\uE8EC";
}
