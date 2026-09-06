add_library(Cue.ProjectFiles STATIC)

target_sources(
    Cue.ProjectFiles
    PRIVATE
        Private/Error.cpp
        Private/Service.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/ProjectFiles/Error.h
            Public/Cue/ProjectFiles/Service.h
)

target_compile_features(Cue.ProjectFiles PUBLIC cxx_std_20)
target_link_libraries(Cue.ProjectFiles PUBLIC Cue.Foundation Cue.IO Cue.Project)
target_include_directories(Cue.ProjectFiles PUBLIC Public)

set_target_properties(
    Cue.ProjectFiles
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.ProjectFiles
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )
    target_link_options(Cue.ProjectFiles PRIVATE $<$<CONFIG:Development>:/DEBUG>)
endif()

get_target_property(cueProjectFilesLinks Cue.ProjectFiles LINK_LIBRARIES)
if(NOT "${cueProjectFilesLinks}" STREQUAL "Cue.Foundation;Cue.IO;Cue.Project")
    message(FATAL_ERROR "Cue.ProjectFiles must depend only on Cue.Foundation, Cue.IO, and Cue.Project: ${cueProjectFilesLinks}")
endif()
