# XAudio2 backend

add_library(xaudio2_backend STATIC
    "XAudio2Backend.h"
    "XAudio2Backend.cpp"
)

target_link_libraries(xaudio2_backend PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    Audio
    win_platform
)
target_link_libraries(xaudio2_backend PRIVATE
    xaudio2
)

target_include_directories(xaudio2_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
