if(NOT DEFINED PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required.")
endif()

if(NOT DEFINED ASSET_ROOT)
    message(FATAL_ERROR "ASSET_ROOT is required.")
endif()

if(NOT DEFINED SCRIPT_ROOT)
    message(FATAL_ERROR "SCRIPT_ROOT is required.")
endif()

if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required.")
endif()

file(GLOB_RECURSE script_headers LIST_DIRECTORIES false "${SCRIPT_ROOT}/*Script.h")
list(SORT script_headers)

set(include_lines "")
set(factory_lines "")
set(script_count 0)

foreach(header_path IN LISTS script_headers)
    file(READ "${header_path}" header_text)

    string(REGEX MATCH
        "MARIONETTE_DECLARE_SCRIPT_TYPE[ \t\r\n]*\\([ \t\r\n]*([A-Za-z_][A-Za-z0-9_]*)[ \t\r\n]*,[ \t\r\n]*\"[^\"]*\"[ \t\r\n]*\\)"
        type_match
        "${header_text}")

    string(REGEX MATCH
        "make_([A-Za-z0-9_]+)_script_definition[ \t\r\n]*\\([ \t\r\n]*\\)[ \t\r\n]*noexcept"
        factory_match
        "${header_text}")

    if(type_match STREQUAL "" AND factory_match STREQUAL "")
        continue()
    endif()

    if(type_match STREQUAL "" OR factory_match STREQUAL "")
        message(FATAL_ERROR
            "Script header is incomplete. Both MARIONETTE_DECLARE_SCRIPT_TYPE and make_*_script_definition are required: ${header_path}")
    endif()

    file(RELATIVE_PATH relative_header_path "${ASSET_ROOT}" "${header_path}")
    cmake_path(CONVERT "${relative_header_path}" TO_CMAKE_PATH_LIST relative_header_path)

    string(REGEX REPLACE
        ".*make_([A-Za-z0-9_]+)_script_definition.*"
        "make_\\1_script_definition"
        factory_name
        "${factory_match}")

    string(APPEND include_lines "#include \"${relative_header_path}\"\n")
    string(APPEND factory_lines "        ${factory_name}(),\n")
    math(EXPR script_count "${script_count} + 1")
endforeach()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

set(output_text "#include \"ScriptRegistry.h\"\n\n")
string(APPEND output_text "${include_lines}")
string(APPEND output_text "\n")
string(APPEND output_text "// === C++ includes ===\n")
string(APPEND output_text "#include <array>\n")
string(APPEND output_text "#include <span>\n\n")
string(APPEND output_text "// *** This file is generated. Do not edit by hand.\n")
string(APPEND output_text "// *** Why: script registration is derived from Assets/Scripts at build time.\n\n")
string(APPEND output_text "namespace\n")
string(APPEND output_text "{\n")
string(APPEND output_text "    const std::array<Cue::Core::Native::ScriptClassDefinition, ${script_count}> k_scriptClasses = {\n")
string(APPEND output_text "${factory_lines}")
string(APPEND output_text "    };\n")
string(APPEND output_text "}\n\n")
string(APPEND output_text "std::span<const Cue::Core::Native::ScriptClassDefinition>\n")
string(APPEND output_text "script_classes() noexcept\n")
string(APPEND output_text "{\n")
string(APPEND output_text "    return std::span<const Cue::Core::Native::ScriptClassDefinition>(\n")
string(APPEND output_text "        k_scriptClasses.data(),\n")
string(APPEND output_text "        k_scriptClasses.size());\n")
string(APPEND output_text "}\n")

file(WRITE "${OUTPUT}" "${output_text}")
