add_executable(App WIN32 "App.cpp" )

target_link_libraries(App PRIVATE cue_warnings)
target_link_libraries(App PRIVATE cue_compile_options)
target_compile_definitions(App PRIVATE CUE_USE_JSON=0)
