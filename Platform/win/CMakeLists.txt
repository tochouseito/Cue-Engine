add_library(win_platform STATIC "win_platform.h" "win_platform.cpp")

target_link_libraries(win_platform PRIVATE cue_warnings)
target_link_libraries(win_platform PRIVATE cue_compile_options)
