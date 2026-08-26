add_library(Cue.Foundation.Windows STATIC)

target_sources(
    Cue.Foundation.Windows
    PRIVATE
        Private/UtfConversion.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/Foundation/Windows/UtfConversion.h
)

target_link_libraries(Cue.Foundation.Windows PUBLIC Cue.Foundation)
target_compile_features(Cue.Foundation.Windows PUBLIC cxx_std_20)

target_include_directories(
    Cue.Foundation.Windows
    PUBLIC
        Public
)

target_compile_definitions(
    Cue.Foundation.Windows
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

set_target_properties(
    Cue.Foundation.Windows
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.Foundation.Windows
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
        Cue.Foundation.Windows
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()
