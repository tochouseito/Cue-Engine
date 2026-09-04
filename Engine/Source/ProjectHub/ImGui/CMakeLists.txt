add_library(Cue.ProjectHub.ImGui STATIC)

target_sources(
    Cue.ProjectHub.ImGui
    PRIVATE
        Private/ProjectHubPresenter.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/ProjectHub/ImGui/ProjectHubPresenter.h
)

target_link_libraries(
    Cue.ProjectHub.ImGui
    PUBLIC
        Cue.ProjectHub
    PRIVATE
        Cue.ImGui.Core
)
target_compile_features(Cue.ProjectHub.ImGui PUBLIC cxx_std_20)
target_include_directories(Cue.ProjectHub.ImGui PUBLIC Public)

set_target_properties(
    Cue.ProjectHub.ImGui
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.ProjectHub.ImGui
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )
    target_link_options(Cue.ProjectHub.ImGui PRIVATE $<$<CONFIG:Development>:/DEBUG>)
endif()
