add_library(Cue.RHI.D3D12.Windows STATIC)

target_sources(
    Cue.RHI.D3D12.Windows
    PRIVATE
        Private/D3d12WindowsPresentation.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/RHI/D3D12/Windows/D3d12WindowsPresentation.h
)

target_link_libraries(
    Cue.RHI.D3D12.Windows
    PUBLIC
        Cue.RHI.D3D12
    PRIVATE
        Cue.Platform.Windows
)
target_compile_features(Cue.RHI.D3D12.Windows PUBLIC cxx_std_20)

target_include_directories(
    Cue.RHI.D3D12.Windows
    PUBLIC
        Public
)

set_target_properties(
    Cue.RHI.D3D12.Windows
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.RHI.D3D12.Windows
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
        Cue.RHI.D3D12.Windows
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()
