# ECS の CMakeLists.txt

# Static ライブラリを作成
add_library(ECS STATIC "ECSManager.h" "ECSManager.cpp" "ComponentIDHelper.h" "ComponentID.cpp")

# リンクするライブラリの指定
target_link_libraries(ECS PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
)

# インクルードディレクトリの追加
target_include_directories(ECS PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
