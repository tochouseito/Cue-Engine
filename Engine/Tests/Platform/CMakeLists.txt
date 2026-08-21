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
get_target_property(cuePlatformManualDependencies Cue.Platform MANUALLY_ADDED_DEPENDENCIES)
get_target_property(cuePlatformInterfaceSources Cue.Platform INTERFACE_SOURCES)
get_target_property(
    cuePlatformInterfaceDirectLinks
    Cue.Platform
    INTERFACE_LINK_LIBRARIES_DIRECT
)
get_target_property(cuePlatformSources Cue.Platform SOURCES)
get_target_property(cuePlatformHeaders Cue.Platform HEADER_SET)
get_target_property(cuePlatformSourceDirectory Cue.Platform SOURCE_DIR)
get_target_property(cuePlatformWindowsLinks Cue.Platform.Windows LINK_LIBRARIES)
get_target_property(
    cuePlatformWindowsInterfaceLinks
    Cue.Platform.Windows
    INTERFACE_LINK_LIBRARIES
)
get_target_property(
    cuePlatformWindowsManualDependencies
    Cue.Platform.Windows
    MANUALLY_ADDED_DEPENDENCIES
)
get_target_property(
    cuePlatformWindowsInterfaceSources
    Cue.Platform.Windows
    INTERFACE_SOURCES
)
get_target_property(
    cuePlatformWindowsInterfaceDirectLinks
    Cue.Platform.Windows
    INTERFACE_LINK_LIBRARIES_DIRECT
)
get_target_property(cuePlatformWindowsSources Cue.Platform.Windows SOURCES)
get_target_property(cuePlatformWindowsHeaders Cue.Platform.Windows HEADER_SET)
get_target_property(
    cuePlatformWindowsSourceDirectory
    Cue.Platform.Windows
    SOURCE_DIR
)

foreach(targetProperty IN ITEMS cuePlatformHeaders cuePlatformWindowsHeaders)
    if(${targetProperty} MATCHES "-NOTFOUND$")
        set(${targetProperty} "")
    endif()
endforeach()

foreach(
    targetProperty
    IN ITEMS
        cuePlatformManualDependencies
        cuePlatformInterfaceSources
        cuePlatformInterfaceDirectLinks
        cuePlatformWindowsManualDependencies
        cuePlatformWindowsInterfaceSources
        cuePlatformWindowsInterfaceDirectLinks
)
    if(${targetProperty} MATCHES "-NOTFOUND$")
        set(${targetProperty} "")
    endif()
endforeach()

if(NOT "${cuePlatformLinks}" STREQUAL "Cue.Foundation")
    message(FATAL_ERROR "Cue.Platform must link only Cue.Foundation: ${cuePlatformLinks}")
endif()

if(NOT "${cuePlatformInterfaceLinks}" STREQUAL "Cue.Foundation")
    message(
        FATAL_ERROR
        "Cue.Platform must expose only Cue.Foundation: ${cuePlatformInterfaceLinks}"
    )
endif()

set(cuePlatformWindowsAllowedLinks Cue.Platform User32)
set(cuePlatformWindowsAllowedLinkOnlyLibraries User32)

foreach(cuePlatformWindowsInterfaceLink IN LISTS cuePlatformWindowsInterfaceLinks)
    if(cuePlatformWindowsInterfaceLink STREQUAL "Cue.Platform")
        continue()
    endif()

    if(cuePlatformWindowsInterfaceLink MATCHES "^\\$<LINK_ONLY:([^>]+)>$")
        list(
            FIND
            cuePlatformWindowsAllowedLinkOnlyLibraries
            "${CMAKE_MATCH_1}"
            cuePlatformWindowsLinkOnlyIndex
        )

        if(NOT cuePlatformWindowsLinkOnlyIndex EQUAL -1)
            continue()
        endif()
    endif()

    message(
        FATAL_ERROR
        "Cue.Platform.Windows contains an unapproved interface dependency: ${cuePlatformWindowsInterfaceLink}"
    )
endforeach()

list(
    FIND
    cuePlatformWindowsInterfaceLinks
    Cue.Platform
    cuePlatformWindowsPlatformInterfaceLinkIndex
)

if(cuePlatformWindowsPlatformInterfaceLinkIndex EQUAL -1)
    message(FATAL_ERROR "Cue.Platform.Windows must expose Cue.Platform")
endif()

foreach(cuePlatformWindowsLink IN LISTS cuePlatformWindowsLinks)
    list(
        FIND
        cuePlatformWindowsAllowedLinks
        "${cuePlatformWindowsLink}"
        cuePlatformWindowsLinkIndex
    )

    if(cuePlatformWindowsLinkIndex EQUAL -1)
        message(
            FATAL_ERROR
            "Cue.Platform.Windows contains an unapproved link dependency: ${cuePlatformWindowsLink}"
        )
    endif()
endforeach()

list(FIND cuePlatformWindowsLinks Cue.Platform cuePlatformWindowsPlatformLinkIndex)

if(cuePlatformWindowsPlatformLinkIndex EQUAL -1)
    message(FATAL_ERROR "Cue.Platform.Windows must link Cue.Platform")
endif()

foreach(
    targetProperty
    IN ITEMS
        cuePlatformManualDependencies
        cuePlatformInterfaceSources
        cuePlatformInterfaceDirectLinks
        cuePlatformWindowsManualDependencies
        cuePlatformWindowsInterfaceSources
        cuePlatformWindowsInterfaceDirectLinks
)
    if(NOT "${${targetProperty}}" STREQUAL "")
        message(
            FATAL_ERROR
            "Platform targets must not expose non-link target edges: ${targetProperty}=${${targetProperty}}"
        )
    endif()
endforeach()

set(cuePlatformTargetNames Cue.Platform Cue.Platform.Windows)
set(cuePlatformTargetSources cuePlatformSources cuePlatformWindowsSources)

foreach(targetIndex RANGE 0 1)
    list(GET cuePlatformTargetNames ${targetIndex} targetName)
    list(GET cuePlatformTargetSources ${targetIndex} targetSourcesVariable)
    set(targetSources "${${targetSourcesVariable}}")

    if(targetSources MATCHES "\\$<TARGET_OBJECTS:" OR targetSources MATCHES "\\$<")
        message(
            FATAL_ERROR
            "${targetName} must not consume generated target sources: ${targetSources}"
        )
    endif()

    foreach(targetSource IN LISTS targetSources)
        get_source_file_property(
            isGeneratedSource
            "${targetSource}"
            TARGET_DIRECTORY ${targetName}
            GENERATED
        )

        if(isGeneratedSource)
            message(
                FATAL_ERROR
                "${targetName} must not consume generated sources: ${targetSource}"
            )
        endif()
    endforeach()
endforeach()

set(cuePlatformTargetFiles "")

foreach(targetFile IN LISTS cuePlatformSources cuePlatformHeaders)
    get_filename_component(
        targetFilePath
        "${targetFile}"
        ABSOLUTE
        BASE_DIR "${cuePlatformSourceDirectory}"
    )
    list(APPEND cuePlatformTargetFiles "${targetFilePath}")
endforeach()

foreach(targetFile IN LISTS cuePlatformWindowsSources cuePlatformWindowsHeaders)
    get_filename_component(
        targetFilePath
        "${targetFile}"
        ABSOLUTE
        BASE_DIR "${cuePlatformWindowsSourceDirectory}"
    )
    list(APPEND cuePlatformTargetFiles "${targetFilePath}")
endforeach()

list(REMOVE_DUPLICATES cuePlatformTargetFiles)
string(REPLACE ";" "\n" cuePlatformTargetFileContent "${cuePlatformTargetFiles}")
set(
    cuePlatformTargetFileList
    "${CMAKE_CURRENT_BINARY_DIR}/Cue.Platform.TargetFiles.txt"
)
file(
    GENERATE
    OUTPUT "${cuePlatformTargetFileList}"
    CONTENT "${cuePlatformTargetFileContent}\n"
)

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
        -DPLATFORM_TARGET_FILE_LIST=${cuePlatformTargetFileList}
        -DWINDOWS_SDK_VERSION=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}
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
