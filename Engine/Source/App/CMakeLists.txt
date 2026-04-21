# App モジュールの CMake 設定

add_executable(CueApp WIN32
    "App/App.cpp"
)

target_link_libraries(CueApp PRIVATE
    cue_warnings
    cue_compile_options
    Base
    win_platform
    d3d12_backend
    Engine
)

target_include_directories(CueApp PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/App"
)

target_compile_definitions(CueApp PRIVATE
    CUE_PROJECT_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

set_target_properties(CueApp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/App/$<CONFIG>"
)

add_custom_command(
    TARGET CueApp
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/dxcompiler.dll"
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/dxil.dll"
            "$<TARGET_FILE_DIR:CueApp>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Shaders"
            "$<TARGET_FILE_DIR:CueApp>/EngineResources/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Fonts"
            "$<TARGET_FILE_DIR:CueApp>/EngineResources/Fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:CueApp>/config"
    VERBATIM
)
