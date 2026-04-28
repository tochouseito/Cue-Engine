find_package(Jolt CONFIG REQUIRED)

add_library(jolt_physics_backend STATIC
    "JoltPhysicsSystem.h"
    "JoltPhysicsSystem.cpp"
)

target_link_libraries(jolt_physics_backend PUBLIC
    cue_warnings
    cue_compile_options
    Physics
    Jolt::Jolt
)

target_include_directories(jolt_physics_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(jolt_physics_backend PRIVATE /FS)
endif()
