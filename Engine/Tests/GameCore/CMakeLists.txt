add_executable(CueGameCoreTests GameCoreTests.cpp)
target_link_libraries(CueGameCoreTests PRIVATE Cue.GameCore)
target_compile_features(CueGameCoreTests PRIVATE cxx_std_20)

add_executable(CueGameCorePublicHeaderTest PublicHeaderCompile.cpp)
target_link_libraries(CueGameCorePublicHeaderTest PRIVATE Cue.GameCore)
target_compile_features(CueGameCorePublicHeaderTest PRIVATE cxx_std_20)

add_executable(CueGameCoreProcessTests GameCoreProcessTests.cpp)
target_link_libraries(CueGameCoreProcessTests PRIVATE Cue.GameCore)
target_compile_features(CueGameCoreProcessTests PRIVATE cxx_std_20)

set(cueGameCoreTestTargets CueGameCoreTests CueGameCorePublicHeaderTest CueGameCoreProcessTests)

foreach(testTarget IN LISTS cueGameCoreTestTargets)
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

add_custom_target(Cue.GameCore.Tests DEPENDS ${cueGameCoreTestTargets})

add_test(NAME Cue.GameCore.Storage COMMAND CueGameCoreTests)
add_test(NAME Cue.GameCore.PublicHeaders COMMAND CueGameCorePublicHeaderTest)
add_test(NAME Cue.GameCore.StructuralMutation.Reentry COMMAND CueGameCoreProcessTests)
set_tests_properties(Cue.GameCore.StructuralMutation.Reentry PROPERTIES WILL_FAIL TRUE)

set_tests_properties(
    Cue.GameCore.Storage
    Cue.GameCore.PublicHeaders
    Cue.GameCore.StructuralMutation.Reentry
    PROPERTIES
        LABELS GameCore
        TIMEOUT 30
)
