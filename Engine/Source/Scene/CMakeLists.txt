add_library(Cue.Scene STATIC)

target_sources(
    Cue.Scene
    PRIVATE
        Private/Error.cpp
        Private/Identity.cpp
        Private/SceneDocument.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/Scene/Error.h
            Public/Cue/Scene/Identity.h
            Public/Cue/Scene/SceneDocument.h
)

target_compile_features(Cue.Scene PUBLIC cxx_std_20)
target_link_libraries(Cue.Scene PUBLIC Cue.Foundation Cue.Math)
target_include_directories(Cue.Scene PUBLIC Public)

set_target_properties(
    Cue.Scene
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.Scene
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )
    target_link_options(Cue.Scene PRIVATE $<$<CONFIG:Development>:/DEBUG>)
endif()

get_target_property(cueSceneLinks Cue.Scene LINK_LIBRARIES)
get_target_property(cueSceneInterfaceLinks Cue.Scene INTERFACE_LINK_LIBRARIES)

if(NOT "${cueSceneLinks}" STREQUAL "Cue.Foundation;Cue.Math")
    message(FATAL_ERROR "Cue.Scene must directly link only Cue.Foundation and Cue.Math: ${cueSceneLinks}")
endif()

if(NOT "${cueSceneInterfaceLinks}" STREQUAL "Cue.Foundation;Cue.Math")
    message(FATAL_ERROR "Cue.Scene must expose only Cue.Foundation and Cue.Math: ${cueSceneInterfaceLinks}")
endif()
