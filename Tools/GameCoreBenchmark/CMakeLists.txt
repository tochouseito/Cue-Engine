# Rebuild ECS の再現可能な M10 比較 Baseline Target を定義する
find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE rebuildCommit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE rebuildCommitResult
)

string(LENGTH "${rebuildCommit}" rebuildCommitLength)

if(NOT rebuildCommitResult EQUAL 0 OR NOT rebuildCommit MATCHES "^[0-9a-fA-F]+$" OR NOT rebuildCommitLength EQUAL 40)
    message(FATAL_ERROR "Failed to resolve the CueEngine Rebuild commit")
endif()

add_executable(CueGameCoreBenchmark)

add_custom_command(
    TARGET CueGameCoreBenchmark
    PRE_BUILD
    COMMAND
        "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DREBUILD_ROOT=${CMAKE_SOURCE_DIR}"
        "-DEXPECTED_COMMIT=${rebuildCommit}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/VerifyRebuildCheckout.cmake"
    COMMENT "Verifying the Rebuild ECS benchmark inputs before compilation"
    VERBATIM
)

target_sources(
    CueGameCoreBenchmark
    PRIVATE
        Main.cpp
)

target_compile_features(CueGameCoreBenchmark PRIVATE cxx_std_20)
target_link_libraries(CueGameCoreBenchmark PRIVATE Cue.GameCore)

target_compile_definitions(
    CueGameCoreBenchmark
    PRIVATE
        CUE_REBUILD_COMMIT="${rebuildCommit}"
)

set_target_properties(
    CueGameCoreBenchmark
    PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

if(MSVC)
    target_compile_options(
        CueGameCoreBenchmark
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )

    target_link_options(
        CueGameCoreBenchmark
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()
