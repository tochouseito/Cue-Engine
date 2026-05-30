# Engine モジュールの CMakeLists

# ターゲットソースの追加
add_library(Engine STATIC
    "Engine.h"
    "Engine.cpp"
 "Command/PlatformCommandContext.h" "Command/PlatformCommandContext.cpp" "FrameController.h" "FrameController.cpp")

# リンクするライブラリの指定
target_link_libraries(Engine PUBLIC
    cue_warnings
    cue_compile_options
    Base
    CueMath
    Core
    PAL
    RHI
)

# インクルードディレクトリの指定
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(Engine PRIVATE /FS /bigobj)
endif()
