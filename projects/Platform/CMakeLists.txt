add_library(Platform STATIC "Platform.h" "Platform.cpp")

target_link_libraries(Platform PUBLIC Core)
target_link_libraries(Platform PRIVATE cue_warnings)
target_link_libraries(Platform PRIVATE cue_compile_options)

target_include_directories(Platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
