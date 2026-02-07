add_library(Engine STATIC "Engine.h" "Engine.cpp")

target_link_libraries(Engine PRIVATE cue_warnings)
target_link_libraries(Engine PRIVATE cue_compile_options)
