
add_executable(Editor WIN32 "Editor.cpp"  "ImGuiManager.h" "ImGuiManager.cpp")

target_link_libraries(Editor PRIVATE cue_warnings)
target_link_libraries(Editor PRIVATE cue_compile_options)
target_link_libraries(Editor PRIVATE Engine)
target_link_libraries(Editor PRIVATE win_platform)
target_link_libraries(Editor PRIVATE d3d12_backend)
target_link_libraries(Editor PRIVATE cue_imgui)

add_custom_command(TARGET Editor POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/thirdparty/dxc/dxcompiler.dll"
          "$<TARGET_FILE_DIR:Editor>/dxcompiler.dll"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/thirdparty/dxc/dxil.dll"
          "$<TARGET_FILE_DIR:Editor>/dxil.dll"
  )
