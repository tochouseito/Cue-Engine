add_library(GraphicsCore STATIC "GraphicsCore.h" "GraphicsCore.cpp")

target_link_libraries(GraphicsCore PRIVATE cue_warnings)
target_link_libraries(GraphicsCore PRIVATE cue_compile_options)
