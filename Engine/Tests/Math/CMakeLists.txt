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

add_test(NAME Cue.Math.ScalarAngleVector COMMAND CueMathTests)
add_test(NAME Cue.Math.MatrixQuaternionTransform COMMAND CueMathTransformTests)
add_test(NAME Cue.Math.Properties COMMAND CueMathPropertyTests)
add_test(NAME Cue.Math.PublicHeaders COMMAND CueMathPublicHeaderTest)
add_test(
    NAME Cue.Math.Dependencies
    COMMAND
        ${CMAKE_COMMAND}
        -DMATH_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Engine/Source/Math
        -DREPOSITORY_ROOT=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyMathDependencies.cmake
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
