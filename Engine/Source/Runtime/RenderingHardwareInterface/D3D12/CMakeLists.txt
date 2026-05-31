# D3D12 モジュールの CMakeLists.txt

# パッケージの検索
find_package(directxtk12 CONFIG REQUIRED)

# ターゲットソースファイルの追加
add_library(d3d12_backend STATIC "D3D12Backend.h" "D3D12Backend.cpp" "DX12Common.h" "DX12RenderDevice.h" "DX12RenderDevice.cpp" "ResourceLeakChecker.h" "DescriptorAllocator.h" "DescriptorAllocator.cpp" "DX12GpuResource.h" "HLSLCompiler.h" "HLSLCompiler.cpp" "DX12BufferManager.h" "DX12BufferManager.cpp" "DX12GpuCommand.h" "DX12GpuCommand.cpp" "DX12PipelineManager.h" "DX12TextureManager.h" "DX12PipelineManager.cpp" "DX12TextureManager.cpp" "DX12ViewManager.h" "DX12ViewManager.cpp" "SwapChain.h" "SwapChain.cpp")

# リンクするライブラリの指定
target_link_libraries(d3d12_backend PUBLIC
  cue_warnings
  cue_compile_options
  Base
  CueMath
  Core
  RHI
  win_platform
  Microsoft::DirectXTK12
)

target_link_libraries(d3d12_backend PRIVATE
    d3d12
    dxgi
    dxcompiler
    dxguid
)

# ターゲットのインクルードディレクトリを設定
target_include_directories(d3d12_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

