add_library(Cue.Math STATIC)

target_sources(
    Cue.Math
    PRIVATE
        Private/Angle.cpp
        Private/Matrix.cpp
        Private/Quaternion.cpp
        Private/Scalar.cpp
        Private/Transform.cpp
        Private/Vector.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/Math/Angle.h
            Public/Cue/Math/Matrix.h
            Public/Cue/Math/Quaternion.h
            Public/Cue/Math/Scalar.h
            Public/Cue/Math/Transform.h
            Public/Cue/Math/Vector.h
)

target_compile_features(Cue.Math PUBLIC cxx_std_20)
target_link_libraries(Cue.Math PUBLIC Cue.Foundation)

target_include_directories(
    Cue.Math
    PUBLIC
        Public
    PRIVATE
        Private
)

set_target_properties(
    Cue.Math
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.Math
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
        Cue.Math
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()

string(
    CONCAT
    cueMathDirectoryCxxFlags
    "${CMAKE_CXX_FLAGS};"
    "${CMAKE_CXX_FLAGS_DEBUG};"
    "${CMAKE_CXX_FLAGS_DEVELOPMENT};"
    "${CMAKE_CXX_FLAGS_RELEASE}"
)

set_property(
    TARGET Cue.Math
    PROPERTY CUE_MATH_DIRECTORY_CXX_FLAGS "${cueMathDirectoryCxxFlags}"
)

set_property(
    TARGET Cue.Math
    PROPERTY CUE_MATH_INCLUDE_CURRENT_DIR "${CMAKE_INCLUDE_CURRENT_DIR}"
)

set_property(
    TARGET Cue.Math
    PROPERTY CUE_MATH_INCLUDE_CURRENT_DIR_IN_INTERFACE
             "${CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE}"
)
