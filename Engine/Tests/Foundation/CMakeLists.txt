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

get_target_property(cueFoundationLinkLibraries Cue.Foundation LINK_LIBRARIES)

if(cueFoundationLinkLibraries MATCHES "-NOTFOUND$")
    set(cueFoundationLinkLibraries "")
elseif(NOT "${cueFoundationLinkLibraries}" STREQUAL "")
    message(
        FATAL_ERROR
        "Cue.Foundation must not have direct link dependencies: ${cueFoundationLinkLibraries}"
    )
endif()

get_target_property(
    cueFoundationInterfaceLinkLibraries
    Cue.Foundation
    INTERFACE_LINK_LIBRARIES
)

if(cueFoundationInterfaceLinkLibraries MATCHES "-NOTFOUND$")
    set(cueFoundationInterfaceLinkLibraries "")
elseif(NOT "${cueFoundationInterfaceLinkLibraries}" STREQUAL "")
    message(
        FATAL_ERROR
        "Cue.Foundation must not expose link dependencies: ${cueFoundationInterfaceLinkLibraries}"
    )
endif()

get_target_property(
    cueFoundationManualDependencies
    Cue.Foundation
    MANUALLY_ADDED_DEPENDENCIES
)

if(cueFoundationManualDependencies MATCHES "-NOTFOUND$")
    set(cueFoundationManualDependencies "")
elseif(NOT "${cueFoundationManualDependencies}" STREQUAL "")
    message(
        FATAL_ERROR
        "Cue.Foundation must not have manual target dependencies: ${cueFoundationManualDependencies}"
    )
endif()

get_target_property(
    cueFoundationInterfaceLinkOptions
    Cue.Foundation
    INTERFACE_LINK_OPTIONS
)

if(cueFoundationInterfaceLinkOptions MATCHES "-NOTFOUND$")
    set(cueFoundationInterfaceLinkOptions "")
elseif(NOT "${cueFoundationInterfaceLinkOptions}" STREQUAL "")
    message(
        FATAL_ERROR
        "Cue.Foundation must not expose link options: ${cueFoundationInterfaceLinkOptions}"
    )
endif()

get_target_property(
    cueFoundationInterfaceDirectLinks
    Cue.Foundation
    INTERFACE_LINK_LIBRARIES_DIRECT
)

if(cueFoundationInterfaceDirectLinks MATCHES "-NOTFOUND$")
    set(cueFoundationInterfaceDirectLinks "")
elseif(NOT "${cueFoundationInterfaceDirectLinks}" STREQUAL "")
    message(
        FATAL_ERROR
        "Cue.Foundation must not inject direct link dependencies: ${cueFoundationInterfaceDirectLinks}"
    )
endif()

get_target_property(cueFoundationInterfaceSources Cue.Foundation INTERFACE_SOURCES)

if(cueFoundationInterfaceSources MATCHES "-NOTFOUND$")
    set(cueFoundationInterfaceSources "")
elseif(NOT "${cueFoundationInterfaceSources}" STREQUAL "")
    message(
        FATAL_ERROR
        "Cue.Foundation must not expose interface sources: ${cueFoundationInterfaceSources}"
    )
endif()

get_target_property(cueFoundationSources Cue.Foundation SOURCES)
string(TOLOWER "${cueFoundationSources}" cueFoundationSourcesLower)

if(cueFoundationSourcesLower MATCHES "\\$<target_objects:")
    message(
        FATAL_ERROR
        "Cue.Foundation must not consume target object sources: ${cueFoundationSources}"
    )
endif()

get_target_property(cueFoundationLinkOptions Cue.Foundation LINK_OPTIONS)

if(cueFoundationLinkOptions MATCHES "-NOTFOUND$")
    set(cueFoundationLinkOptions "")
endif()

string(TOLOWER "${cueFoundationLinkOptions}" cueFoundationLinkOptionsLower)

if(cueFoundationLinkOptionsLower MATCHES "defaultlib:|\\.lib([ >;$]|$)")
    message(
        FATAL_ERROR
        "Cue.Foundation must not inject libraries through link options: ${cueFoundationLinkOptions}"
    )
endif()

set(cueFoundationLinkLibrariesDisplay "<none>")
set(cueFoundationInterfaceLinkLibrariesDisplay "<none>")
set(cueFoundationManualDependenciesDisplay "<none>")
set(cueFoundationInterfaceLinkOptionsDisplay "<none>")
set(cueFoundationInterfaceDirectLinksDisplay "<none>")
set(cueFoundationInterfaceSourcesDisplay "<none>")
set(cueFoundationTargetObjectSourcesDisplay "<none>")

if("${cueFoundationLinkOptions}" STREQUAL "")
    set(cueFoundationLinkOptionsDisplay "<none>")
else()
    set(cueFoundationLinkOptionsDisplay "${cueFoundationLinkOptions}")
endif()
set(
    cueFoundationDependencyReport
    "${CMAKE_CURRENT_BINARY_DIR}/Cue.Foundation.Dependencies.txt"
)

configure_file(
    FoundationDependencyReport.txt.in
    "${cueFoundationDependencyReport}"
    @ONLY
)

configure_file(
    CMakeGraphVizOptions.cmake
    "${CMAKE_BINARY_DIR}/CMakeGraphVizOptions.cmake"
    COPYONLY
)

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

add_custom_target(
    Cue.Foundation.Tests
    DEPENDS
        CueFoundationEmergencyTests
        CueFoundationLoggerTests
        CueFoundationDiagnosticsProcessTests
        CueFoundationAssertTests
        CueFoundationResultTests
        CueFoundationPublicHeaderTest
)

add_test(NAME Cue.Foundation.Result COMMAND CueFoundationResultTests)
add_test(NAME Cue.Foundation.PublicHeaders COMMAND CueFoundationPublicHeaderTest)
add_test(NAME Cue.Foundation.Logger COMMAND CueFoundationLoggerTests)
add_test(NAME Cue.Foundation.Assert.Success COMMAND CueFoundationAssertTests Success)

get_filename_component(cueFoundationToolDirectory "${CMAKE_LINKER}" DIRECTORY)
find_program(
    cueFoundationDumpbin
    NAMES dumpbin.exe
    HINTS "${cueFoundationToolDirectory}"
    NO_DEFAULT_PATH
    REQUIRED
)

add_test(
    NAME Cue.Foundation.Dependencies
    COMMAND
        ${CMAKE_COMMAND}
        -DREPORT_FILE=${cueFoundationDependencyReport}
        -DFOUNDATION_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Engine/Source/Foundation
        -DREPOSITORY_ROOT=${PROJECT_SOURCE_DIR}
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DTARGET_GRAPH_FILE=${CMAKE_BINARY_DIR}/CueEngine.TargetGraph.dot
        -DWINDOWS_SDK_VERSION=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}
        "-DDUMPBIN_EXECUTABLE=${cueFoundationDumpbin}"
        "-DFOUNDATION_LIBRARY=$<TARGET_FILE:Cue.Foundation>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyFoundationDependencies.cmake
)

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
    Cue.Foundation.Dependencies
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
