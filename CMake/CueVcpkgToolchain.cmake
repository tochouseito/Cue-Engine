# CueEngineが検証済みvcpkg Toolchainだけを使用する

set(CUE_VCPKG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/.tools/vcpkg")
cmake_path(NORMAL_PATH CUE_VCPKG_ROOT)

set(CUE_VCPKG_TOOLCHAIN "${CUE_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
if(NOT EXISTS "${CUE_VCPKG_TOOLCHAIN}")
    message(
        FATAL_ERROR
        "Pinned vcpkg toolchain is missing. Run Tools/Dependencies/RestoreVcpkg.ps1 first."
    )
endif()

set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "Disable implicit dependency restore" FORCE)
include("${CUE_VCPKG_TOOLCHAIN}")
