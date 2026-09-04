add_library(Cue.ProjectHub STATIC)

target_sources(
    Cue.ProjectHub
    PRIVATE
        Private/Error.cpp
        Private/Service.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/ProjectHub/Error.h
            Public/Cue/ProjectHub/Service.h
)

target_compile_features(Cue.ProjectHub PUBLIC cxx_std_20)
target_link_libraries(Cue.ProjectHub PUBLIC Cue.Foundation Cue.IO Cue.Project)
target_include_directories(Cue.ProjectHub PUBLIC Public)

set_target_properties(
    Cue.ProjectHub
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.ProjectHub
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )
    target_link_options(Cue.ProjectHub PRIVATE $<$<CONFIG:Development>:/DEBUG>)
endif()

if(WIN32)
    add_subdirectory(Windows)
endif()

add_subdirectory(ImGui)

get_target_property(cueProjectHubLinks Cue.ProjectHub LINK_LIBRARIES)
if(NOT "${cueProjectHubLinks}" STREQUAL "Cue.Foundation;Cue.IO;Cue.Project")
    message(FATAL_ERROR "Cue.ProjectHub must depend only on Cue.Foundation, Cue.IO, and Cue.Project: ${cueProjectHubLinks}")
endif()
