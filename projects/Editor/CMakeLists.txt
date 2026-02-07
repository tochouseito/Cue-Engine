
add_executable(Editor WIN32 "Editor.cpp" )

target_link_libraries(Editor PRIVATE cue_warnings)
target_link_libraries(Editor PRIVATE cue_compile_options)
target_link_libraries(Editor PRIVATE Engine)
target_link_libraries(Editor PRIVATE win_platform)
target_link_libraries(Editor PRIVATE d3d12_backend)
