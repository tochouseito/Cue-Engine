add_library(Cue.Schema STATIC)

target_sources(
    Cue.Schema
    PRIVATE
        Private/Descriptor.cpp
        Private/Error.cpp
        Private/Registry.cpp
        Private/Types.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/Schema/Descriptor.h
            Public/Cue/Schema/Error.h
            Public/Cue/Schema/Registry.h
            Public/Cue/Schema/Types.h
)

target_compile_features(Cue.Schema PUBLIC cxx_std_20)
target_link_libraries(Cue.Schema PUBLIC Cue.Foundation)
target_include_directories(Cue.Schema PUBLIC Public)

set_target_properties(
    Cue.Schema
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.Schema
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )
    target_link_options(Cue.Schema PRIVATE $<$<CONFIG:Development>:/DEBUG>)
endif()

get_target_property(cueSchemaLinks Cue.Schema LINK_LIBRARIES)
get_target_property(cueSchemaInterfaceLinks Cue.Schema INTERFACE_LINK_LIBRARIES)

if(NOT "${cueSchemaLinks}" STREQUAL "Cue.Foundation")
    message(FATAL_ERROR "Cue.Schema must directly link only Cue.Foundation: ${cueSchemaLinks}")
endif()

if(NOT "${cueSchemaInterfaceLinks}" STREQUAL "Cue.Foundation")
    message(FATAL_ERROR "Cue.Schema must expose only Cue.Foundation: ${cueSchemaInterfaceLinks}")
endif()
