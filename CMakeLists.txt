# CueEngine の最上位 CMake プロジェクトグローバル構成

# CMake の最小バージョンを指定
cmake_minimum_required(VERSION 4.2.0)

# サポートされている場合は、MSVC コンパイラのホットリロードを有効にします
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Visual Studio の中間ファイル出力先（IntDir）
# MSBuild マクロを使って構成名/プロジェクト名ごとに分離する
if (CMAKE_GENERATOR MATCHES "Visual Studio")
  list(APPEND CMAKE_VS_GLOBALS
    "IntDir=${CMAKE_SOURCE_DIR}/generated/intermediate/$(Configuration)/$(ProjectName)/"
  )
endif()

# プロジェクトの名前とバージョンを定義
project(CueEngine VERSION 0.0.1 LANGUAGES C CXX)

option(CUE_BUILD_EDITOR "Build the editor application and editor-specific runtime code" OFF)

# vcpkg のホストトリプレットを定義（環境変数 VCPKG_HOST_TRIPLET が定義されていない場合はデフォルト値を使用）
if (NOT DEFINED VCPKG_HOST_TRIPLET OR VCPKG_HOST_TRIPLET STREQUAL "")
  set(VCPKG_HOST_TRIPLET "x64-windows")
endif()
set(CUE_VCPKG_HOST_BIN_DIR
  "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_HOST_TRIPLET}/bin")

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

# 生成物の出力先（全ターゲット共通）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/Binaries/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/Binaries/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/Sdk/Lib/$<CONFIG>")

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

# 特定の警告を無効化またはエラー扱いにするオプション
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
    /Zc:preprocessor # 標準準拠のプリプロセッサを使用
)

# 定数定義
target_compile_definitions(cue_compile_options INTERFACE
    UNICODE
    _UNICODE
    $<$<CONFIG:Release>:CUE_RELEASE=1>
    $<$<CONFIG:RelWithDebInfo>:CUE_RELWITHDEBINFO=1>
    $<$<CONFIG:Debug>:CUE_DEBUG=1>
)

if (CUE_BUILD_EDITOR)
  target_compile_definitions(cue_compile_options INTERFACE
      CUE_EDITOR=1
  )
endif()

# 追加のインクルードディレクトリ
target_include_directories(cue_compile_options INTERFACE
    "${CMAKE_SOURCE_DIR}/Source/ThirdParty"
)

# サブディレクトリの追加
add_subdirectory("Engine/Source/Runtime")
if (CUE_BUILD_EDITOR)
  add_subdirectory("Engine/Source/Editor")
endif()
