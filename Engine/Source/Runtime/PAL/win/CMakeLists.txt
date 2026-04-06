# windows platform の CMakeLists.txt

# スタティックライブラリを作成
add_library(win_platform STATIC "WinPlatform.h" "WinPlatform.cpp" "App/WinApp.h" "App/WinApp.cpp" "IO/WinFileSystem.h" "IO/WinFileSystem.cpp" "Threading/WinThread.h" "Threading/WinThread.cpp" "Threading/WinThreadFactory.h" "Threading/WinThreadFactory.cpp" "Time/WinQpcClock.h" "Time/WinQpcClock.cpp" "Time/WinWaiter.h" "Time/WinWaiter.cpp" "stdafx.h" "ConvertHresult.h" "ConvertUTF.h" "IO/WinFile.h" "IO/WinFile.cpp")

# リンクするライブラリの指定
target_link_libraries(win_platform PUBLIC
    cue_warnings
    cue_compile_options
    Base
    CueMath
    Core
    PAL
)
target_link_libraries(win_platform PRIVATE
    winmm
)

# インクルードディレクトリの追加
target_include_directories(win_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# マクロ
target_compile_definitions(win_platform PRIVATE
    CUE_PLATFORM_WINDOWS
)
