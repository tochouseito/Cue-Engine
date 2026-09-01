add_executable(CueProjectTests ProjectTests.cpp)
target_link_libraries(CueProjectTests PRIVATE Cue.Project)
target_compile_features(CueProjectTests PRIVATE cxx_std_20)

add_executable(CueProjectPublicHeaderTest PublicHeaderCompile.cpp)
target_link_libraries(CueProjectPublicHeaderTest PRIVATE Cue.Project)
target_compile_features(CueProjectPublicHeaderTest PRIVATE cxx_std_20)

add_executable(CueProjectGeneratorTests GeneratorTests.cpp)
target_link_libraries(CueProjectGeneratorTests PRIVATE Cue.Project)
target_compile_features(CueProjectGeneratorTests PRIVATE cxx_std_20)

add_executable(CueProjectRegistryTests RegistryTests.cpp)
target_link_libraries(CueProjectRegistryTests PRIVATE Cue.Project)
target_compile_features(CueProjectRegistryTests PRIVATE cxx_std_20)

set(cueProjectTestTargets CueProjectTests CueProjectPublicHeaderTest CueProjectGeneratorTests CueProjectRegistryTests)
if(WIN32)
    add_executable(CueProjectGeneratorWindowsTests GeneratorWindowsTests.cpp)
    target_link_libraries(CueProjectGeneratorWindowsTests PRIVATE Cue.Project Cue.IO.Windows)
    target_compile_features(CueProjectGeneratorWindowsTests PRIVATE cxx_std_20)
    list(APPEND cueProjectTestTargets CueProjectGeneratorWindowsTests)
endif()
foreach(testTarget IN LISTS cueProjectTestTargets)
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

add_custom_target(Cue.Project.Tests DEPENDS ${cueProjectTestTargets})

add_test(NAME Cue.Project.Descriptor COMMAND CueProjectTests)
set_tests_properties(Cue.Project.Descriptor PROPERTIES LABELS Project TIMEOUT 20)

add_test(NAME Cue.Project.PublicHeaders COMMAND CueProjectPublicHeaderTest)
set_tests_properties(Cue.Project.PublicHeaders PROPERTIES LABELS Project TIMEOUT 10)

add_test(NAME Cue.Project.Generator COMMAND CueProjectGeneratorTests)
set_tests_properties(Cue.Project.Generator PROPERTIES LABELS Project TIMEOUT 20)

add_test(NAME Cue.Project.Registry COMMAND CueProjectRegistryTests)
set_tests_properties(Cue.Project.Registry PROPERTIES LABELS Project TIMEOUT 20)

if(WIN32)
    add_test(NAME Cue.Project.Generator.Windows COMMAND CueProjectGeneratorWindowsTests)
    set_tests_properties(Cue.Project.Generator.Windows PROPERTIES LABELS Project TIMEOUT 20)
endif()
