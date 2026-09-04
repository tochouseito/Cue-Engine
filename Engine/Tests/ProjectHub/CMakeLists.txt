add_executable(CueProjectHubPublicHeaderTest PublicHeaderCompile.cpp)
target_link_libraries(CueProjectHubPublicHeaderTest PRIVATE Cue.ProjectHub)
target_compile_features(CueProjectHubPublicHeaderTest PRIVATE cxx_std_20)

set(cueProjectHubTestTargets CueProjectHubPublicHeaderTest)

if(WIN32)
    add_executable(CueProjectHubTests ProjectHubTests.cpp)
    target_link_libraries(CueProjectHubTests PRIVATE Cue.ProjectHub Cue.IO.Windows)
    target_compile_features(CueProjectHubTests PRIVATE cxx_std_20)
    list(APPEND cueProjectHubTestTargets CueProjectHubTests)

    add_executable(
        CueProjectHubWindowsPublicHeaderTest
        WindowsProjectHubPlatformHeaderCompile.cpp
    )
    target_link_libraries(CueProjectHubWindowsPublicHeaderTest PRIVATE Cue.ProjectHub.Windows)
    target_compile_features(CueProjectHubWindowsPublicHeaderTest PRIVATE cxx_std_20)
    list(APPEND cueProjectHubTestTargets CueProjectHubWindowsPublicHeaderTest)

    add_executable(
        CueProjectHubWindowsTests
        WindowsProjectHubPlatformTests.cpp
    )
    target_link_libraries(
        CueProjectHubWindowsTests
        PRIVATE
            Cue.Foundation.Windows
            Cue.ProjectHub.Windows
    )
    target_compile_features(CueProjectHubWindowsTests PRIVATE cxx_std_20)
    list(APPEND cueProjectHubTestTargets CueProjectHubWindowsTests)
endif()

foreach(testTarget IN LISTS cueProjectHubTestTargets)
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
        target_link_options(${testTarget} PRIVATE $<$<CONFIG:Development>:/DEBUG>)
    endif()
endforeach()

add_custom_target(Cue.ProjectHub.Tests DEPENDS ${cueProjectHubTestTargets})

add_test(NAME Cue.ProjectHub.PublicHeaders COMMAND CueProjectHubPublicHeaderTest)
set_tests_properties(Cue.ProjectHub.PublicHeaders PROPERTIES LABELS ProjectHub TIMEOUT 10)

if(WIN32)
    add_test(NAME Cue.ProjectHub.Service COMMAND CueProjectHubTests)
    add_test(NAME Cue.ProjectHub.Windows.PublicHeaders COMMAND CueProjectHubWindowsPublicHeaderTest)
    add_test(NAME Cue.ProjectHub.Windows.Platform COMMAND CueProjectHubWindowsTests)
    set_tests_properties(
        Cue.ProjectHub.Service
        Cue.ProjectHub.Windows.PublicHeaders
        Cue.ProjectHub.Windows.Platform
        PROPERTIES
            LABELS ProjectHub
            TIMEOUT 30
    )
endif()
