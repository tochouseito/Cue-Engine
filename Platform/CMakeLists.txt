add_library(Platform STATIC "Platform.h" "Platform.cpp")

target_link_libraries(Platform PRIVATE cue_warnings)
target_link_libraries(Platform PRIVATE cue_compile_options)
