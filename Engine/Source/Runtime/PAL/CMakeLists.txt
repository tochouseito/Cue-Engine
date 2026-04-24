# Platform Abstraction Layer

# スタティックライブラリを作成
add_library(PAL STATIC "PAL.cpp" "PAL.h" "PlatformFactory.h" "PlatformMessage.h" "Input/Keyboard.h" "Input/InputManager.h" "Input/InputManager.cpp")

# リンクするライブラリの指定
target_link_libraries(PAL PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
)

# インクルードディレクトリの追加
target_include_directories(PAL PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# サブディレクトリの追加
add_subdirectory("Win")
