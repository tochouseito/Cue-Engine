add_library(Cue.Foundation STATIC)

target_sources(
    Cue.Foundation
    PRIVATE
        Private/Assert.cpp
        Private/ConsoleLogSink.cpp
        Private/Error.cpp
        Private/Fatal.cpp
        Private/Logger.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/Foundation/Capability.h
            Public/Cue/Foundation/EmergencyHandler.h
            Public/Cue/Foundation/Error.h
            Public/Cue/Foundation/Assert.h
            Public/Cue/Foundation/Fatal.h
            Public/Cue/Foundation/Log.h
            Public/Cue/Foundation/NumberParsing.h
            Public/Cue/Foundation/Result.h
            Public/Cue/Foundation/SourceLocation.h
            Public/Cue/Foundation/Types.h
)

target_compile_features(Cue.Foundation PUBLIC cxx_std_20)

target_compile_definitions(
    Cue.Foundation
    PUBLIC
        $<$<CONFIG:Debug>:CUE_ENABLE_ASSERTS=1>
        $<$<CONFIG:Development>:CUE_ENABLE_ASSERTS=1>
        $<$<CONFIG:Release>:CUE_ENABLE_ASSERTS=0>
        $<$<CONFIG:Debug>:CUE_ENABLE_DEBUG_BREAK=1>
        $<$<CONFIG:Development>:CUE_ENABLE_DEBUG_BREAK=0>
        $<$<CONFIG:Release>:CUE_ENABLE_DEBUG_BREAK=0>
)

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

if(WIN32)
    add_subdirectory(Windows)
endif()
