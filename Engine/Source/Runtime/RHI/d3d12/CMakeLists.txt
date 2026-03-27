# DirectX12 モジュールの CMakeLists.txt

# Static ライブラリを作成
add_library(d3d12_backend STATIC "d3d12_backend.h" "d3d12_backend.cpp" "stdafx.h" "DX12RenderDevice.h" "DX12RenderDevice.cpp" "DescriptorAllocator.h" "DescriptorAllocator.cpp" "ResourceLeakChecker.h" "HLSLCompiler.h" "HLSLCompiler.cpp" "DX12BufferManager.h" "DX12TextureManager.h" "DX12BufferManager.cpp" "DX12TextureManager.cpp" "DX12PipelineManager.h" "DX12PipelineManager.cpp" "DX12ViewManager.h" "DX12ViewManager.cpp" "DX12GpuResource.h" "ResourceUploader.h" "ResourceUploader.cpp" "DX12GpuCommand.h" "DX12GpuCommand.cpp")

# リンクするライブラリの指定
target_link_libraries(d3d12_backend PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    RHI
    win_platform
)
target_link_libraries(d3d12_backend PRIVATE
    d3d12
    dxgi
    dxcompiler
    dxguid
)

# インクルードディレクトリの追加
target_include_directories(d3d12_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# マクロ
target_compile_definitions(d3d12_backend PRIVATE
    CUE_BACKEND_DX12
)
