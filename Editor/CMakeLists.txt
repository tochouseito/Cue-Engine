
add_executable(Editor WIN32 "Editor.cpp" )

target_link_libraries(Editor PRIVATE cue_warnings)
target_link_libraries(Editor PRIVATE cue_compile_options)
