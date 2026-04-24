# Audio abstraction layer

add_library(Audio STATIC
    "Audio.h"
    "Audio.cpp"
    "AudioBackendFactory.h"
)

target_link_libraries(Audio PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
)

target_include_directories(Audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory("XAudio2")
