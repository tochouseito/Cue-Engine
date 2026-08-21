get_target_property(cueRuntimeHostLinks CueRuntimeHost LINK_LIBRARIES)

set(
    cueRuntimeHostDependencyReport
    "${CMAKE_CURRENT_BINARY_DIR}/CueRuntimeHost.Dependencies.txt"
)

configure_file(
    RuntimeHostDependencyReport.txt.in
    "${cueRuntimeHostDependencyReport}"
    @ONLY
)

add_custom_target(
    Cue.RuntimeHost.Tests
    DEPENDS CueRuntimeHost
)

add_test(
    NAME Cue.RuntimeHost.Smoke
    COMMAND
        CueRuntimeHost
        --smoke-test
        --title "CueEngine Runtime Host Smoke"
        --width 640
        --height 360
)

add_test(
    NAME Cue.RuntimeHost.Dependencies
    COMMAND
        ${CMAKE_COMMAND}
        -DREPORT_FILE=${cueRuntimeHostDependencyReport}
        -DRUNTIME_HOST_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Engine/Source/RuntimeHost
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyRuntimeHostDependencies.cmake
)

add_test(
    NAME Cue.RuntimeHost.InitializationFailure
    COMMAND
        ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:CueRuntimeHost>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyRuntimeHostFailure.cmake
)

set_tests_properties(
    Cue.RuntimeHost.Smoke
    PROPERTIES
        LABELS "RuntimeHost;Smoke"
        TIMEOUT 10
)

set_tests_properties(
    Cue.RuntimeHost.Dependencies
    Cue.RuntimeHost.InitializationFailure
    PROPERTIES
        LABELS RuntimeHost
        TIMEOUT 10
)
