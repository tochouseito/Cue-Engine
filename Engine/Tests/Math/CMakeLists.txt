get_target_property(cueMathLinks Cue.Math LINK_LIBRARIES)
get_target_property(cueMathInterfaceLinks Cue.Math INTERFACE_LINK_LIBRARIES)

if(NOT "${cueMathLinks}" STREQUAL "Cue.Foundation")
    message(FATAL_ERROR "Cue.Math must directly link only Cue.Foundation: ${cueMathLinks}")
endif()

if(NOT "${cueMathInterfaceLinks}" STREQUAL "Cue.Foundation")
    message(
        FATAL_ERROR
        "Cue.Math must expose only Cue.Foundation: ${cueMathInterfaceLinks}"
    )
endif()

add_executable(CueMathTests MathTests.cpp)
target_link_libraries(CueMathTests PRIVATE Cue.Math)
target_compile_features(CueMathTests PRIVATE cxx_std_20)

add_executable(CueMathTransformTests MathTransformTests.cpp)
target_link_libraries(CueMathTransformTests PRIVATE Cue.Math)
target_compile_features(CueMathTransformTests PRIVATE cxx_std_20)

add_executable(CueMathPropertyTests MathPropertyTests.cpp)
target_link_libraries(CueMathPropertyTests PRIVATE Cue.Math)
target_compile_features(CueMathPropertyTests PRIVATE cxx_std_20)

add_executable(CueMathDependencyTests MathDependencyTests.cpp)
target_compile_features(CueMathDependencyTests PRIVATE cxx_std_20)

add_executable(
    CueMathPublicHeaderTest
    AngleHeaderCompile.cpp
    MatrixHeaderCompile.cpp
    PublicHeaderCompile.cpp
    QuaternionHeaderCompile.cpp
    ScalarHeaderCompile.cpp
    TransformHeaderCompile.cpp
    VectorHeaderCompile.cpp
)

target_link_libraries(CueMathPublicHeaderTest PRIVATE Cue.Math)
target_compile_features(CueMathPublicHeaderTest PRIVATE cxx_std_20)

set(
    cueMathTestTargets
    CueMathTests
    CueMathTransformTests
    CueMathPropertyTests
    CueMathDependencyTests
    CueMathPublicHeaderTest
)

foreach(testTarget IN LISTS cueMathTestTargets)
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

add_custom_target(Cue.Math.Tests DEPENDS ${cueMathTestTargets})

set(
    cueMathBuildPropertiesPath
    "${CMAKE_CURRENT_BINARY_DIR}/CueMathBuildProperties-$<CONFIG>.txt"
)

string(
    CONCAT
    cueMathBuildPropertiesContent
    "PRECOMPILE_HEADERS=$<TARGET_PROPERTY:Cue.Math,PRECOMPILE_HEADERS>\n"
    "INTERFACE_PRECOMPILE_HEADERS=$<TARGET_PROPERTY:Cue.Math,INTERFACE_PRECOMPILE_HEADERS>\n"
    "COMPILE_DEFINITIONS=$<TARGET_PROPERTY:Cue.Math,COMPILE_DEFINITIONS>\n"
    "INTERFACE_COMPILE_DEFINITIONS=$<TARGET_PROPERTY:Cue.Math,INTERFACE_COMPILE_DEFINITIONS>\n"
    "COMPILE_OPTIONS=$<TARGET_PROPERTY:Cue.Math,COMPILE_OPTIONS>\n"
    "COMPILE_FLAGS=$<TARGET_PROPERTY:Cue.Math,COMPILE_FLAGS>\n"
    "INTERFACE_COMPILE_OPTIONS=$<TARGET_PROPERTY:Cue.Math,INTERFACE_COMPILE_OPTIONS>\n"
    "INCLUDE_DIRECTORIES=$<TARGET_PROPERTY:Cue.Math,INCLUDE_DIRECTORIES>\n"
    "INTERFACE_INCLUDE_DIRECTORIES=$<TARGET_PROPERTY:Cue.Math,INTERFACE_INCLUDE_DIRECTORIES>\n"
    "LINK_LIBRARIES=$<TARGET_PROPERTY:Cue.Math,LINK_LIBRARIES>\n"
    "INTERFACE_LINK_LIBRARIES=$<TARGET_PROPERTY:Cue.Math,INTERFACE_LINK_LIBRARIES>\n"
    "CMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}\n"
    "CMAKE_CXX_FLAGS_DEBUG=${CMAKE_CXX_FLAGS_DEBUG}\n"
    "CMAKE_CXX_FLAGS_DEVELOPMENT=${CMAKE_CXX_FLAGS_DEVELOPMENT}\n"
    "CMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}\n"
    "DIRECTORY_CXX_FLAGS=$<TARGET_PROPERTY:Cue.Math,CUE_MATH_DIRECTORY_CXX_FLAGS>\n"
)

get_target_property(cueMathSources Cue.Math SOURCES)
get_target_property(cueMathSourceDirectory Cue.Math SOURCE_DIR)
set(
    cueMathAuditedSourceExtensions
    .c++
    .cc
    .cpp
    .cppm
    .cxx
    .cxxm
    .h
    .h++
    .hh
    .hpp
    .hxx
    .inl
    .ipp
    .ixx
    .tpp
)

foreach(
    targetInjectionProperty
    UNITY_BUILD_CODE_BEFORE_INCLUDE
    UNITY_BUILD_CODE_AFTER_INCLUDE
    VS_USER_PROPS
)
    get_target_property(
        targetInjectionValue
        Cue.Math
        ${targetInjectionProperty}
    )

    if(targetInjectionValue)
        message(
            FATAL_ERROR
            "Cue.Math target injection property cannot be audited: ${targetInjectionProperty}"
        )
    endif()
endforeach()

foreach(
    precompiledHeaderProperty
    PRECOMPILE_HEADERS
    INTERFACE_PRECOMPILE_HEADERS
)
    get_target_property(
        cueMathPrecompiledHeaders
        Cue.Math
        ${precompiledHeaderProperty}
    )

    if(cueMathPrecompiledHeaders MATCHES "-NOTFOUND$")
        set(cueMathPrecompiledHeaders "")
    endif()

    foreach(precompiledHeader IN LISTS cueMathPrecompiledHeaders)
        if(precompiledHeader MATCHES "\\$<")
            message(
                FATAL_ERROR
                "Cue.Math precompiled header generator expression cannot be audited: ${precompiledHeader}"
            )
        endif()

        if(precompiledHeader MATCHES "^<[^>]+>$")
            continue()
        endif()

        get_filename_component(
            precompiledHeaderAbsolutePath
            "${precompiledHeader}"
            ABSOLUTE
            BASE_DIR "${cueMathSourceDirectory}"
        )
        get_filename_component(
            precompiledHeaderExtension
            "${precompiledHeaderAbsolutePath}"
            LAST_EXT
        )
        string(TOLOWER "${precompiledHeaderExtension}" precompiledHeaderExtension)
        list(
            FIND
            cueMathAuditedSourceExtensions
            "${precompiledHeaderExtension}"
            precompiledHeaderExtensionIndex
        )

        if(precompiledHeaderExtensionIndex EQUAL -1)
            message(
                FATAL_ERROR
                "Cue.Math precompiled header extension cannot be audited: ${precompiledHeaderAbsolutePath}"
            )
        endif()

        file(
            RELATIVE_PATH
            precompiledHeaderRelativePath
            "${cueMathSourceDirectory}"
            "${precompiledHeaderAbsolutePath}"
        )

        if(precompiledHeaderRelativePath MATCHES "^\\.\\.(/|$)")
            message(
                FATAL_ERROR
                "Cue.Math precompiled header must remain under its source directory: ${precompiledHeaderAbsolutePath}"
            )
        endif()

        get_source_file_property(
            precompiledHeaderIsGenerated
            "${precompiledHeaderAbsolutePath}"
            TARGET_DIRECTORY Cue.Math
            GENERATED
        )

        if(precompiledHeaderIsGenerated)
            message(
                FATAL_ERROR
                "Cue.Math generated precompiled header cannot be audited: ${precompiledHeaderAbsolutePath}"
            )
        endif()
    endforeach()
endforeach()

foreach(sourcePath IN LISTS cueMathSources)
    if(sourcePath MATCHES "\\$<")
        message(
            FATAL_ERROR
            "Cue.Math source generator expression cannot be audited: ${sourcePath}"
        )
    endif()

    get_filename_component(
        sourceAbsolutePath
        "${sourcePath}"
        ABSOLUTE
        BASE_DIR "${cueMathSourceDirectory}"
    )

    get_filename_component(sourceExtension "${sourceAbsolutePath}" LAST_EXT)
    string(TOLOWER "${sourceExtension}" sourceExtension)
    list(FIND cueMathAuditedSourceExtensions "${sourceExtension}" sourceExtensionIndex)

    if(sourceExtensionIndex EQUAL -1)
        message(
            FATAL_ERROR
            "Cue.Math source extension cannot be audited: ${sourceAbsolutePath}"
        )
    endif()

    file(
        RELATIVE_PATH
        sourceRelativeToMath
        "${cueMathSourceDirectory}"
        "${sourceAbsolutePath}"
    )

    if(sourceRelativeToMath MATCHES "^\\.\\.(/|$)")
        message(
            FATAL_ERROR
            "Cue.Math source must remain under its source directory: ${sourceAbsolutePath}"
        )
    endif()

    get_source_file_property(
        sourceIsGenerated
        "${sourceAbsolutePath}"
        TARGET_DIRECTORY Cue.Math
        GENERATED
    )

    if(sourceIsGenerated)
        message(
            FATAL_ERROR
            "Cue.Math generated source cannot be audited: ${sourceAbsolutePath}"
        )
    endif()

    foreach(
        sourceProperty
        COMPILE_DEFINITIONS
        COMPILE_DEFINITIONS_DEBUG
        COMPILE_DEFINITIONS_DEVELOPMENT
        COMPILE_DEFINITIONS_RELEASE
        COMPILE_OPTIONS
        COMPILE_FLAGS
        INCLUDE_DIRECTORIES
        VS_SETTINGS
    )
        get_source_file_property(
            sourcePropertyValue
            "${sourceAbsolutePath}"
            TARGET_DIRECTORY Cue.Math
            ${sourceProperty}
        )

        if(sourcePropertyValue STREQUAL "NOTFOUND")
            set(sourcePropertyValue "")
        endif()

        string(
            APPEND
            cueMathBuildPropertiesContent
            "SOURCE[${sourcePath}].${sourceProperty}=${sourcePropertyValue}\n"
        )
    endforeach()
endforeach()

file(
    GENERATE
    OUTPUT "${cueMathBuildPropertiesPath}"
    CONTENT "${cueMathBuildPropertiesContent}"
)

add_test(NAME Cue.Math.ScalarAngleVector COMMAND CueMathTests)
add_test(NAME Cue.Math.MatrixQuaternionTransform COMMAND CueMathTransformTests)
add_test(NAME Cue.Math.Properties COMMAND CueMathPropertyTests)
add_test(NAME Cue.Math.PublicHeaders COMMAND CueMathPublicHeaderTest)
add_test(
    NAME Cue.Math.Dependencies
    COMMAND
        CueMathDependencyTests
        ${PROJECT_SOURCE_DIR}
        "${cueMathBuildPropertiesPath}"
)

set_tests_properties(
    Cue.Math.ScalarAngleVector
    Cue.Math.MatrixQuaternionTransform
    Cue.Math.Properties
    Cue.Math.PublicHeaders
    Cue.Math.Dependencies
    PROPERTIES
        LABELS Math
        TIMEOUT 10
)
