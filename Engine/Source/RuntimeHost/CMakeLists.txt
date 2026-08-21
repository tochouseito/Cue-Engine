add_executable(CueRuntimeHost)

target_sources(
    CueRuntimeHost
    PRIVATE
        Main.cpp
)

target_link_libraries(
    CueRuntimeHost
    PRIVATE
        Cue.Foundation
        Cue.Platform.Windows
)
target_compile_features(CueRuntimeHost PRIVATE cxx_std_20)

set_target_properties(
    CueRuntimeHost
    PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

if(MSVC)
    target_compile_options(
        CueRuntimeHost
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
        CueRuntimeHost
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()
