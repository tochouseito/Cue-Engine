# D3D12 モジュールの CMakeLists.txt

# パッケージの検索
find_package(directxtk12 CONFIG REQUIRED)

# ターゲットソースファイルの追加
add_library(d3d12_backend STATIC "D3D12Backend.h" "D3D12Backend.cpp" "DX12Common.h")

# リンクするライブラリの指定
target_link_libraries(d3d12_backend
  cue_warnings
  cue_compile_options
  Base
  CueMath
  Core
  RHI
  win_platform
  Microsoft::DirectXTK12
)

# ターゲットのインクルードディレクトリを設定
target_include_directories(d3d12_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

