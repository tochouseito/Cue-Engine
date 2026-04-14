# GameScript module

set(CUE_ENGINE_ROOT "C:/Users/sinse/source/repos/CueEngine")

if(NOT EXISTS "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core/Native/ScriptAbi.h")
    message(FATAL_ERROR "Cue Engine root not found: ${CUE_ENGINE_ROOT}")
endif()

add_library(GameScript SHARED
    "Source/GameScriptModule.cpp"
)

target_include_directories(GameScript PRIVATE
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core"
)

target_compile_definitions(GameScript PRIVATE
    CUE_SCRIPT_DLL_EXPORTS=1
)

if(MSVC)
    target_compile_options(GameScript PRIVATE
        /utf-8
        /W4
        /EHsc
        /wd4201
    )
endif()

set_target_properties(GameScript PROPERTIES
    OUTPUT_NAME "GameScript"
)
