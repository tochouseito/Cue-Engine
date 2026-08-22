add_library(Cue.RHI STATIC)

target_sources(
    Cue.RHI
    PRIVATE
        Private/GraphicsBackend.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/RHI/GraphicsBackend.h
)

target_link_libraries(Cue.RHI PUBLIC Cue.Foundation)
target_compile_features(Cue.RHI PUBLIC cxx_std_20)

target_include_directories(
    Cue.RHI
    PUBLIC
        Public
    PRIVATE
        Private
)

set_target_properties(
    Cue.RHI
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.RHI
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
        Cue.RHI
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()

if(WIN32)
    add_subdirectory(D3D12)
endif()
