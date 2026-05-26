# Base モジュールの CMakeLists.txt

# Static ライブラリを作成
add_library(Base STATIC "CueResult.h" "CueAssert.h" "CueAssert.cpp")

# リンクするライブラリの指定
target_link_libraries(Base PUBLIC
    cue_warnings
    cue_compile_options
)

# Base ディレクトリをインクルードパスに追加
target_include_directories(Base PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
