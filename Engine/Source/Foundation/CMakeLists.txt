add_library(Cue.Foundation STATIC)

target_sources(
    Cue.Foundation
    PRIVATE
        Private/Error.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/Foundation/EmergencyHandler.h
            Public/Cue/Foundation/Error.h
            Public/Cue/Foundation/Result.h
            Public/Cue/Foundation/SourceLocation.h
            Public/Cue/Foundation/Types.h
)

target_compile_features(Cue.Foundation PUBLIC cxx_std_20)

target_include_directories(
    Cue.Foundation
    PUBLIC
        Public
    PRIVATE
        Private
)

set_target_properties(
    Cue.Foundation
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.Foundation
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
        Cue.Foundation
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()
