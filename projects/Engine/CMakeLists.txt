add_library(Engine STATIC "Engine.h" "Engine.cpp" "frame/FrameController.h" "frame/FrameController.cpp" "Configuration.h" "pass/BackBufferClear.h" "pass/TestDraw.h" "asset/AssetManager.h" "asset/AssetManager.cpp")

target_link_libraries(Engine PRIVATE cue_warnings)
target_link_libraries(Engine PRIVATE cue_compile_options)
target_link_libraries(Engine PUBLIC Core)
target_link_libraries(Engine PUBLIC Platform)
target_link_libraries(Engine PUBLIC GraphicsCore)

# third party
target_link_libraries(Engine PRIVATE cue_json)

target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
