# Core モジュールの CMakeLists.txt

# ターゲットソースの追加
add_library(Core STATIC "CQRS/CQRS.h" "CQRS/CQRS.cpp" "IO/Logger.h" "IO/Logger.cpp" "IO/IFileSystem.h" "IO/Path.h" "IO/Path.cpp" "Threading/IThread.h" "Threading/IThreadFactory.h" "Threading/StopToken.h" "Threading/JobSystem.h" "Time/Timer.h" "Time/IClock.h" "Time/IWaiter.h" "Time/FrameCounter.h" "DebugTool/Profiler.h" "Native/Handle.h" "Container/RingBuffer.h" "Container/Registry.h" "Container/Pool.h")

# リンクするライブラリの指定
target_link_libraries(Core PUBLIC
    cue_warnings
    cue_compile_options
    Base
    CueMath
)

# インクルードディレクトリの指定
target_include_directories(Core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
