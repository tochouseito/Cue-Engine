add_library (Base STATIC "include/Assert.h" )

target_link_libraries(Base PRIVATE cue_warnings)
target_link_libraries(Base PRIVATE cue_compile_options)

target_include_directories(Base PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
