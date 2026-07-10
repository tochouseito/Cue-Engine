# windows platform モジュールの CMakeLists.txt

# ターゲットソースの追加
add_library(win_platform STATIC
    "App/WinApp.cpp"
    "App/WinApp.h"
    "ConvertHresult.h"
    "ConvertUTF.h"
    "Dialog/WinDialogService.cpp"
    "Dialog/WinDialogService.h"
    "IO/WinFile.cpp"
    "IO/WinFile.h"
    "IO/WinFileSystem.cpp"
    "IO/WinFileSystem.h"
    "WinCommon.h"
    "Threading/WinThread.cpp"
    "Threading/WinThread.h"
    "Threading/WinThreadFactory.cpp"
    "Threading/WinThreadFactory.h"
    "Time/WInWaiter.h"
    "Time/WinQpcClock.cpp"
    "Time/WinQpcClock.h"
    "Time/WinWaiter.cpp"
    "win_platform.cpp"
    "win_platform.h"
 "DebugTool/CPUProfiler.h")

# リンクするライブラリの指定
target_link_libraries(win_platform PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    PAL
)
#  固有リンク
target_link_libraries(win_platform PUBLIC
    winmm
    ole32
    shell32
    uuid
    Psapi
)

# インクルードディレクトリの指定
target_include_directories(win_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# MSVC でビルドする場合、/FS オプションを追加して、複数のソースファイルが同時にプリコンパイルされたヘッダーファイルを使用できるようにする
if (MSVC)
    target_compile_options(win_platform PRIVATE /FS)
endif()
