# Physics abstraction layer

add_library(Physics STATIC
    "Physics.h"
    "Physics.cpp"
    "Interfaces.h"
)

target_link_libraries(Physics PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    CueMath
)

target_include_directories(Physics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory("Jolt")
