add_library(d3d12_backend STATIC "d3d12_backend.h" "d3d12_backend.cpp")

target_link_libraries(d3d12_backend PRIVATE cue_warnings)
target_link_libraries(d3d12_backend PRIVATE cue_compile_options)
