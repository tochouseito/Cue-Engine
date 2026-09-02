add_executable(CueSchemaTests SchemaTests.cpp)
target_link_libraries(CueSchemaTests PRIVATE Cue.Schema)
target_compile_features(CueSchemaTests PRIVATE cxx_std_20)

add_executable(CueSchemaPublicHeaderTest PublicHeaderCompile.cpp)
target_link_libraries(CueSchemaPublicHeaderTest PRIVATE Cue.Schema)
target_compile_features(CueSchemaPublicHeaderTest PRIVATE cxx_std_20)

set(cueSchemaTestTargets CueSchemaTests CueSchemaPublicHeaderTest)

foreach(testTarget IN LISTS cueSchemaTestTargets)
    set_target_properties(
        ${testTarget}
        PROPERTIES
            CXX_EXTENSIONS OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    )

    if(MSVC)
        target_compile_options(
            ${testTarget}
            PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /utf-8
                $<$<CONFIG:Development>:/O2>
                $<$<CONFIG:Development>:/Zi>
        )
        target_link_options(${testTarget} PRIVATE $<$<CONFIG:Development>:/DEBUG>)
    endif()
endforeach()

add_custom_target(Cue.Schema.Tests DEPENDS ${cueSchemaTestTargets})

add_test(NAME Cue.Schema.Registry COMMAND CueSchemaTests)
add_test(NAME Cue.Schema.PublicHeaders COMMAND CueSchemaPublicHeaderTest)

set_tests_properties(
    Cue.Schema.Registry
    Cue.Schema.PublicHeaders
    PROPERTIES
        LABELS Schema
        TIMEOUT 20
)
