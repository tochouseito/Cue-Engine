# 構成の種類
add_library(d3d12_backend STATIC "d3d12_backend.h" "d3d12_backend.cpp" "../BackendFactory.h" "private/stdafx.h" "private/RenderDevice.h" "private/RenderDevice.cpp" "private/stdafx.cpp" "private/GpuCommand.h" "private/GpuCommand.cpp")

# リンクライブラリ
target_link_libraries(d3d12_backend PRIVATE cue_warnings)
target_link_libraries(d3d12_backend PRIVATE cue_compile_options)
target_link_libraries(d3d12_backend PUBLIC GraphicsCore)
target_link_libraries(d3d12_backend PRIVATE win_platform)
target_link_libraries(d3d12_backend PRIVATE d3d12)
target_link_libraries(d3d12_backend PRIVATE dxguid)
target_link_libraries(d3d12_backend PRIVATE dxgi)

# インクルードディレクトリ
target_include_directories(d3d12_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(d3d12_backend PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")

# マクロ
target_compile_definitions(d3d12_backend PUBLIC
    BACKEND_DX12
)
