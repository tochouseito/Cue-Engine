add_executable(
    CuePlatformPublicHeaderTest
    PublicHeaderCompile.cpp
    WindowEventHeaderCompile.cpp
    WindowHeaderCompile.cpp
    WindowSystemHeaderCompile.cpp
)

target_link_libraries(CuePlatformPublicHeaderTest PRIVATE Cue.Platform)
target_compile_features(CuePlatformPublicHeaderTest PRIVATE cxx_std_20)

add_executable(
    CuePlatformWindowsPublicHeaderTest
    PublicHeaderCompile.cpp
    WindowsPlatformHeaderCompile.cpp
)

target_link_libraries(CuePlatformWindowsPublicHeaderTest PRIVATE Cue.Platform.Windows)
target_compile_features(CuePlatformWindowsPublicHeaderTest PRIVATE cxx_std_20)

foreach(
    testTarget
    IN ITEMS
        CuePlatformPublicHeaderTest
        CuePlatformWindowsPublicHeaderTest
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

get_target_property(cuePlatformLinks Cue.Platform LINK_LIBRARIES)
get_target_property(cuePlatformInterfaceLinks Cue.Platform INTERFACE_LINK_LIBRARIES)
get_target_property(cuePlatformWindowsLinks Cue.Platform.Windows LINK_LIBRARIES)
get_target_property(
    cuePlatformWindowsInterfaceLinks
    Cue.Platform.Windows
    INTERFACE_LINK_LIBRARIES
)

if(NOT "${cuePlatformLinks}" STREQUAL "Cue.Foundation")
    message(FATAL_ERROR "Cue.Platform must link only Cue.Foundation: ${cuePlatformLinks}")
endif()

if(NOT "${cuePlatformInterfaceLinks}" STREQUAL "Cue.Foundation")
    message(
        FATAL_ERROR
        "Cue.Platform must expose only Cue.Foundation: ${cuePlatformInterfaceLinks}"
    )
endif()

if(NOT "${cuePlatformWindowsLinks}" STREQUAL "Cue.Platform")
    message(
        FATAL_ERROR
        "Cue.Platform.Windows must link only Cue.Platform: ${cuePlatformWindowsLinks}"
    )
endif()

if(NOT "${cuePlatformWindowsInterfaceLinks}" STREQUAL "Cue.Platform")
    message(
        FATAL_ERROR
        "Cue.Platform.Windows must expose only Cue.Platform: ${cuePlatformWindowsInterfaceLinks}"
    )
endif()

set(
    cuePlatformDependencyReport
    "${CMAKE_CURRENT_BINARY_DIR}/Cue.Platform.Dependencies.txt"
)

configure_file(
    PlatformDependencyReport.txt.in
    "${cuePlatformDependencyReport}"
    @ONLY
)

add_custom_target(
    Cue.Platform.Tests
    DEPENDS
        CuePlatformPublicHeaderTest
        CuePlatformWindowsPublicHeaderTest
)

add_test(NAME Cue.Platform.PublicHeaders COMMAND CuePlatformPublicHeaderTest)
add_test(NAME Cue.Platform.Windows.PublicHeaders COMMAND CuePlatformWindowsPublicHeaderTest)

add_test(
    NAME Cue.Platform.Dependencies
    COMMAND
        ${CMAKE_COMMAND}
        -DREPORT_FILE=${cuePlatformDependencyReport}
        -DPLATFORM_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Engine/Source/Platform
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyPlatformDependencies.cmake
)

set_tests_properties(
    Cue.Platform.PublicHeaders
    Cue.Platform.Windows.PublicHeaders
    Cue.Platform.Dependencies
    PROPERTIES
        LABELS Platform
        TIMEOUT 10
)
