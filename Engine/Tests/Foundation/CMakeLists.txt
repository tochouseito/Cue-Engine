add_executable(CueFoundationResultTests ResultTests.cpp)

target_link_libraries(CueFoundationResultTests PRIVATE Cue.Foundation)
target_compile_features(CueFoundationResultTests PRIVATE cxx_std_20)

add_executable(
    CueFoundationPublicHeaderTest
    EmergencyHandlerHeaderCompile.cpp
    ErrorHeaderCompile.cpp
    PublicHeaderCompile.cpp
    ResultHeaderCompile.cpp
    SourceLocationHeaderCompile.cpp
    TypesHeaderCompile.cpp
)

target_link_libraries(CueFoundationPublicHeaderTest PRIVATE Cue.Foundation)
target_compile_features(CueFoundationPublicHeaderTest PRIVATE cxx_std_20)

foreach(testTarget IN ITEMS CueFoundationResultTests CueFoundationPublicHeaderTest)
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

        target_link_options(
            ${testTarget}
            PRIVATE
                $<$<CONFIG:Development>:/DEBUG>
        )
    endif()
endforeach()

add_test(NAME Cue.Foundation.Result COMMAND CueFoundationResultTests)
add_test(NAME Cue.Foundation.PublicHeaders COMMAND CueFoundationPublicHeaderTest)

set_tests_properties(
    Cue.Foundation.Result
    Cue.Foundation.PublicHeaders
    PROPERTIES
        LABELS Foundation
        TIMEOUT 10
)
