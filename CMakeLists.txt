# CMakeList.txt : 最上位の CMake プロジェクト ファイル。グローバル構成を行います
# また、サブプロジェクトをここに含めます。
#
cmake_minimum_required (VERSION 4.2.0)

# サポートされている場合は、MSVC コンパイラのホット リロードを有効にします。
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("CueEngine" LANGUAGES C CXX)

# C標準
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# C++標準
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ランタイムライブラリ
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# 警告設定
add_library(cue_warnings INTERFACE)

# 警告レベル＆エラー扱い設定
target_compile_options(cue_warnings INTERFACE
    /W4 # 警告レベル4
    /WX # 警告をエラーとして扱う
    /external:anglebrackets # 山かっこの外部ヘッダーを指定
    /external:W0 # 外部ヘッダーの警告を無効化
)

#
target_compile_options(cue_warnings INTERFACE
    /wd4201
    /we26800
)

# コンパイルオプション
add_library(cue_compile_options INTERFACE)

target_compile_options(cue_compile_options INTERFACE
    /utf-8 # ソースファイルの文字エンコードをUTF-8に設定
    /MP # 複数プロセスでのビルドを有効化
)

# サブプロジェクトを含めます。
add_subdirectory ("Core")
