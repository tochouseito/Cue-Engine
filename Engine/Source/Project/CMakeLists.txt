add_library(Cue.Project STATIC)

target_sources(
    Cue.Project
    PRIVATE
        Private/Descriptor.cpp
        Private/Error.cpp
        Private/Generator.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/Project/Descriptor.h
            Public/Cue/Project/Error.h
            Public/Cue/Project/Generator.h
)

target_link_libraries(Cue.Project PUBLIC Cue.Foundation Cue.IO)
target_compile_features(Cue.Project PUBLIC cxx_std_20)
target_include_directories(Cue.Project PUBLIC Public)

set_target_properties(
    Cue.Project
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.Project
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )
    target_link_options(Cue.Project PRIVATE $<$<CONFIG:Development>:/DEBUG>)
endif()

get_target_property(cueProjectLinks Cue.Project LINK_LIBRARIES)
if(NOT "${cueProjectLinks}" STREQUAL "Cue.Foundation;Cue.IO")
    message(FATAL_ERROR "Cue.Project must depend only on Cue.Foundation and Cue.IO: ${cueProjectLinks}")
endif()
