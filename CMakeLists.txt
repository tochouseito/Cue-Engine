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

# 生成物の出力先（全ターゲット共通）
# Multi-config generator では $<CONFIG> を含めて二重階層化を防ぐ
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/$<CONFIG>")

# Visual Studio の中間ファイル出力先（IntDir）
# MSBuild マクロを使って構成名/プロジェクト名ごとに分離する
if (CMAKE_GENERATOR MATCHES "Visual Studio")
  list(APPEND CMAKE_VS_GLOBALS
    "IntDir=${CMAKE_SOURCE_DIR}/generated/intermediate/$(Configuration)/$(ProjectName)/"
  )
endif()

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
# 外部ライブラリの警告を無効化するためのオプション
add_library(cue_external_warnings INTERFACE)
target_compile_options(cue_external_warnings INTERFACE
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

# JSON
add_library(cue_json INTERFACE)
target_include_directories(cue_json SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/thirdparty/json")

# ImGui
add_library(cue_imgui STATIC
    "thirdparty/imgui/src/imgui.cpp"
    "thirdparty/imgui/src/imgui_draw.cpp"
    "thirdparty/imgui/src/imgui_tables.cpp"
    "thirdparty/imgui/src/imgui_widgets.cpp"
    "thirdparty/imgui/src/imgui_impl_win32.cpp"
    "thirdparty/imgui/src/imgui_impl_dx12.cpp")
target_include_directories(cue_imgui PUBLIC
    "${CMAKE_SOURCE_DIR}/thirdparty/imgui/include")
target_link_libraries(cue_imgui PRIVATE cue_external_warnings)
target_link_libraries(cue_imgui PRIVATE cue_compile_options)

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
