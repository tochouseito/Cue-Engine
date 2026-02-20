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
    /we28251
)

# コンパイルオプション
add_library(cue_compile_options INTERFACE)

target_compile_options(cue_compile_options INTERFACE
    /utf-8 # ソースファイルの文字エンコードをUTF-8に設定
    /MP # 複数プロセスでのビルドを有効化
)

target_compile_definitions(cue_compile_options INTERFACE
    UNICODE
    _UNICODE
    $<$<CONFIG:Release>:CUE_RELEASE=1>
    $<$<CONFIG:RelWithDebInfo>:CUE_RELWITHDEBINFO=1>
    $<$<CONFIG:Debug>:CUE_DEBUG=1>
)

# JSON（Editor 向け）
# Release では include を通さず、JSON 依存をコンパイル時に禁止する。
add_library(cue_json INTERFACE)
target_include_directories(cue_json SYSTEM INTERFACE
    "$<$<NOT:$<CONFIG:Release>>:${CMAKE_SOURCE_DIR}/thirdparty/json>"
)
target_compile_definitions(cue_json INTERFACE
    "$<$<NOT:$<CONFIG:Release>>:CUE_USE_JSON=1>"
    "$<$<CONFIG:Release>:CUE_USE_JSON=0>"
)

# サブプロジェクトを含めます。
add_subdirectory ("projects/Base")
add_subdirectory ("projects/Math")
add_subdirectory ("projects/Core")
add_subdirectory ("projects/Platform")
add_subdirectory ("projects/Platform/win")
add_subdirectory ("projects/GraphicsCore")
add_subdirectory ("projects/GraphicsCore/d3d12")
add_subdirectory ("projects/Engine")
add_subdirectory ("projects/Editor")
add_subdirectory ("projects/App")
