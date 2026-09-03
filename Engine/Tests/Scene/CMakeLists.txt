add_executable(CueSceneTests SceneTests.cpp)
target_link_libraries(CueSceneTests PRIVATE Cue.Scene)
target_compile_features(CueSceneTests PRIVATE cxx_std_20)

add_executable(CueSceneComponentDataTests ComponentDataTests.cpp)
target_link_libraries(CueSceneComponentDataTests PRIVATE Cue.Scene)
target_compile_features(CueSceneComponentDataTests PRIVATE cxx_std_20)

add_executable(CueScenePublicHeaderTest PublicHeaderCompile.cpp)
target_link_libraries(CueScenePublicHeaderTest PRIVATE Cue.Scene)
target_compile_features(CueScenePublicHeaderTest PRIVATE cxx_std_20)

add_executable(CueSceneSerializationTests SerializationTests.cpp)
target_link_libraries(CueSceneSerializationTests PRIVATE Cue.Scene)
target_compile_features(CueSceneSerializationTests PRIVATE cxx_std_20)

add_executable(CueSceneInstantiationTests InstantiationTests.cpp)
target_link_libraries(CueSceneInstantiationTests PRIVATE Cue.Scene)
target_compile_features(CueSceneInstantiationTests PRIVATE cxx_std_20)

add_executable(CueSceneInstantiationProcessTests InstantiationProcessTests.cpp)
target_link_libraries(CueSceneInstantiationProcessTests PRIVATE Cue.Scene)
target_compile_features(CueSceneInstantiationProcessTests PRIVATE cxx_std_20)

set(cueSceneTestTargets CueSceneTests CueSceneComponentDataTests CueScenePublicHeaderTest CueSceneSerializationTests CueSceneInstantiationTests CueSceneInstantiationProcessTests)

foreach(testTarget IN LISTS cueSceneTestTargets)
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

add_custom_target(Cue.Scene.Tests DEPENDS ${cueSceneTestTargets})

add_test(NAME Cue.Scene.Document COMMAND CueSceneTests)
add_test(NAME Cue.Scene.ComponentData COMMAND CueSceneComponentDataTests)
add_test(NAME Cue.Scene.PublicHeaders COMMAND CueScenePublicHeaderTest)
add_test(NAME Cue.Scene.Serialization COMMAND CueSceneSerializationTests)
add_test(NAME Cue.Scene.Instantiation COMMAND CueSceneInstantiationTests)

foreach(processMode IN ITEMS LiveDestructor LiveMoveAssignment)
    add_test(
        NAME Cue.Scene.Process.${processMode}
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueSceneInstantiationProcessTests>
            -DTEST_MODE=${processMode}
            -DEXPECTED_EXIT_CODE=42
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyProcessExit.cmake
    )
endforeach()

foreach(processMode IN ITEMS EndedDestructor MovedFromDestructor)
    add_test(
        NAME Cue.Scene.Process.${processMode}
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueSceneInstantiationProcessTests>
            -DTEST_MODE=${processMode}
            -DEXPECTED_EXIT_CODE=0
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyProcessExit.cmake
    )
endforeach()

set_tests_properties(
    Cue.Scene.Document
    Cue.Scene.ComponentData
    Cue.Scene.PublicHeaders
    Cue.Scene.Serialization
    Cue.Scene.Instantiation
    Cue.Scene.Process.LiveDestructor
    Cue.Scene.Process.LiveMoveAssignment
    Cue.Scene.Process.EndedDestructor
    Cue.Scene.Process.MovedFromDestructor
    PROPERTIES
        LABELS Scene
        TIMEOUT 10
)
