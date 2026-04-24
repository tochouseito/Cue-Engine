# スタティックライブラリの追加
add_library(Core STATIC "Core.h" "Core.cpp" "Container/Pool.h" "Container/Registry.h" "Container/RingBuffer.h" "IO/IFileSystem.h" "IO/Path.h" "IO/Path.cpp" "IO/Logger.h" "Native/EngineNativeStruct.h" "Native/Handle.h" "Native/ScriptAbi.h" "Threading/IThread.h" "Threading/IThreadFactory.h" "Threading/JobSystem.h" "Threading/StopToken.h" "Time/FrameCounter.h" "Time/IClock.h" "Time/IWaiter.h" "Time/Timer.h" "IO/Logger.cpp" "CQRS/CQRS.h" "Container/BatchedRegistry.h")

# リンクするライブラリの指定
target_link_libraries(Core PUBLIC
    cue_warnings
    cue_compile_options
    Base
    CueMath
)

# 追加のインクルードディレクトリの指定
target_include_directories(Core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# プリコンパイラヘッダの設定
target_precompile_headers(Core PRIVATE "Core_pch.h")
