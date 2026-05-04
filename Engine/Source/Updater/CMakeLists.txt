# CueUpdater は壊れた Editor/Engine を更新するため、Runtime/Core/PAL へ依存させない。

add_executable(CueUpdater WIN32
    "App/CueUpdater.cpp"
)

target_compile_features(CueUpdater PRIVATE cxx_std_20)

target_compile_options(CueUpdater PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /WX /permissive- /Zc:preprocessor>
)

target_compile_definitions(CueUpdater PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    CUE_UPDATER_DEFAULT_REPOSITORY="tochouseito/CueEngine"
)

target_link_libraries(CueUpdater PRIVATE
    winhttp
    bcrypt
    shell32
)

set_target_properties(CueUpdater PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/Updater/$<CONFIG>"
)
