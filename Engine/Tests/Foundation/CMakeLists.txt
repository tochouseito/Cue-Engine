add_executable(CueFoundationResultTests ResultTests.cpp)

target_link_libraries(CueFoundationResultTests PRIVATE Cue.Foundation)
target_compile_features(CueFoundationResultTests PRIVATE cxx_std_20)

add_executable(CueFoundationEmergencyTests EmergencyFailureTests.cpp)

target_link_libraries(CueFoundationEmergencyTests PRIVATE Cue.Foundation)
target_compile_features(CueFoundationEmergencyTests PRIVATE cxx_std_20)

add_executable(CueFoundationLoggerTests LoggerTests.cpp)

target_link_libraries(CueFoundationLoggerTests PRIVATE Cue.Foundation)
target_compile_features(CueFoundationLoggerTests PRIVATE cxx_std_20)

add_executable(CueFoundationDiagnosticsProcessTests DiagnosticsProcessTests.cpp)

target_link_libraries(CueFoundationDiagnosticsProcessTests PRIVATE Cue.Foundation)
target_compile_features(CueFoundationDiagnosticsProcessTests PRIVATE cxx_std_20)

add_executable(CueFoundationAssertTests AssertBehaviorTests.cpp)

target_link_libraries(CueFoundationAssertTests PRIVATE Cue.Foundation)
target_compile_features(CueFoundationAssertTests PRIVATE cxx_std_20)

add_executable(
    CueFoundationPublicHeaderTest
    AssertHeaderCompile.cpp
    EmergencyHandlerHeaderCompile.cpp
    ErrorHeaderCompile.cpp
    FatalHeaderCompile.cpp
    LogHeaderCompile.cpp
    PublicHeaderCompile.cpp
    ResultHeaderCompile.cpp
    SourceLocationHeaderCompile.cpp
    TypesHeaderCompile.cpp
)

target_link_libraries(CueFoundationPublicHeaderTest PRIVATE Cue.Foundation)
target_compile_features(CueFoundationPublicHeaderTest PRIVATE cxx_std_20)

foreach(
    testTarget
    IN ITEMS
        CueFoundationEmergencyTests
        CueFoundationLoggerTests
        CueFoundationDiagnosticsProcessTests
        CueFoundationAssertTests
        CueFoundationResultTests
        CueFoundationPublicHeaderTest
)
    set_target_properties(
        ${testTarget}
        PROPERTIES
            CXX_EXTENSIONS OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    )

    if(MSVC)
        target_compile_options(
            ${testTarget}
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
            ${testTarget}
            PRIVATE
                $<$<CONFIG:Development>:/DEBUG>
        )
    endif()
endforeach()

add_test(NAME Cue.Foundation.Result COMMAND CueFoundationResultTests)
add_test(NAME Cue.Foundation.PublicHeaders COMMAND CueFoundationPublicHeaderTest)
add_test(NAME Cue.Foundation.Logger COMMAND CueFoundationLoggerTests)
add_test(NAME Cue.Foundation.Assert.Success COMMAND CueFoundationAssertTests Success)

add_test(
    NAME Cue.Foundation.Assert.Failure
    COMMAND
        ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:CueFoundationAssertTests>
        -DTEST_MODE=Failure
        -DEXPECTED_EXIT_CODE=$<IF:$<CONFIG:Release>,78,75>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyEmergencyExit.cmake
)

foreach(
    diagnosticMode
    IN ITEMS
        Fatal
        FatalContended
        FatalSinkFailure
        Reentry
        ThrowingSink
)
    if(diagnosticMode STREQUAL "Fatal" OR diagnosticMode STREQUAL "FatalSinkFailure")
        set(expectedDiagnosticExit 75)
    else()
        set(expectedDiagnosticExit 76)
    endif()

    add_test(
        NAME Cue.Foundation.Diagnostics.${diagnosticMode}
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueFoundationDiagnosticsProcessTests>
            -DTEST_MODE=${diagnosticMode}
            -DEXPECTED_EXIT_CODE=${expectedDiagnosticExit}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyEmergencyExit.cmake
    )
endforeach()

foreach(
    emergencyMode
    IN ITEMS
        AddContext
        CreateError
        CreateErrorCode
        Reclassify
)
    add_test(
        NAME Cue.Foundation.Emergency.${emergencyMode}
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueFoundationEmergencyTests>
            -DTEST_MODE=${emergencyMode}
            -DEXPECTED_EXIT_CODE=73
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyEmergencyExit.cmake
    )
endforeach()

set_tests_properties(
    Cue.Foundation.Result
    Cue.Foundation.PublicHeaders
    Cue.Foundation.Logger
    Cue.Foundation.Assert.Success
    PROPERTIES
        LABELS Foundation
        TIMEOUT 10
)

set_tests_properties(
    Cue.Foundation.Assert.Failure
    Cue.Foundation.Diagnostics.Fatal
    Cue.Foundation.Diagnostics.FatalContended
    Cue.Foundation.Diagnostics.FatalSinkFailure
    Cue.Foundation.Diagnostics.Reentry
    Cue.Foundation.Diagnostics.ThrowingSink
    PROPERTIES
        LABELS "Foundation;Diagnostics;Process"
        TIMEOUT 10
)

set_tests_properties(
    Cue.Foundation.Emergency.AddContext
    Cue.Foundation.Emergency.CreateError
    Cue.Foundation.Emergency.CreateErrorCode
    Cue.Foundation.Emergency.Reclassify
    PROPERTIES
        LABELS "Foundation;Emergency"
        TIMEOUT 10
)
