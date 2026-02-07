add_library(Engine STATIC "Engine.h" "Engine.cpp")

target_link_libraries(Engine PRIVATE cue_warnings)
target_link_libraries(Engine PRIVATE cue_compile_options)
target_link_libraries(Engine PUBLIC Core)
target_link_libraries(Engine PUBLIC Platform)
target_link_libraries(Engine PUBLIC GraphicsCore)

target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
