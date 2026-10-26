# Lumen Engine application: the editor + game host (`lumen`) and the standalone player (`lumen-player`).
set(IMGUI_DIR ${LUMEN_WICKED_DIR}/Samples/Example_ImGui_Docking/ImGui)
set(LUMEN_RUNTIME_DATA_DIR ${CMAKE_SOURCE_DIR}/runtime_data)

add_library(lumen_imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/ImGuizmo.cpp)
target_include_directories(lumen_imgui PUBLIC ${IMGUI_DIR} ${LUMEN_WICKED_DIR}/Samples/Example_ImGui_Docking)
target_compile_definitions(lumen_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
if(WIN32)
  target_sources(lumen_imgui PRIVATE ${IMGUI_DIR}/imgui_impl_win32.cpp)
else()
  find_package(SDL2 REQUIRED)
  target_sources(lumen_imgui PRIVATE ${IMGUI_DIR}/imgui_impl_sdl.cpp)
  target_link_libraries(lumen_imgui PUBLIC SDL2::SDL2)
endif()

set(LUMEN_ENGINE_SOURCES
  src/App.cpp src/Diagnostics.cpp src/NativeBuild.cpp src/Editor.cpp src/Bridge.cpp src/Play.cpp src/LuaEdit.cpp src/CodeLang.cpp src/LuaLsp.cpp src/NavSystem.cpp
  src/ui/Backend.cpp src/ui/Theme.cpp
  src/panels/Hub.cpp src/panels/Viewport.cpp src/panels/Hierarchy.cpp src/panels/Inspector.cpp
  src/panels/Shell.cpp src/panels/Assets.cpp src/panels/Console.cpp src/panels/CodeEditor.cpp src/panels/Tools.cpp src/panels/Debug.cpp src/panels/Timeline.cpp src/ClipSystem.cpp src/panels/TerrainTool.cpp
  ${LUMEN_WICKED_DIR}/Editor/ModelImporter_OBJ.cpp ${LUMEN_WICKED_DIR}/Editor/ModelImporter_GLTF.cpp
  ${LUMEN_WICKED_DIR}/Editor/ModelImporter_FBX.cpp ${LUMEN_WICKED_DIR}/Editor/ModelImporter_PLY.cpp
  ${LUMEN_WICKED_DIR}/Editor/miniply.cpp)

add_library(lumen_engine STATIC ${LUMEN_ENGINE_SOURCES})
target_include_directories(lumen_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(lumen_engine PRIVATE ${CMAKE_SOURCE_DIR}/core/src ${LUMEN_LUA_INCLUDE_DIR} ${LUMEN_WICKED_DIR}/Editor)
target_link_libraries(lumen_engine PUBLIC lumen_core lumen_imgui WickedEngine Threads::Threads)
target_compile_definitions(lumen_engine PUBLIC LUMEN_RUNTIME_DATA_DIR="${LUMEN_RUNTIME_DATA_DIR}")
if(WIN32)
  target_link_libraries(lumen_engine PUBLIC dbghelp)
endif()

if(WIN32)
  add_executable(lumen WIN32 src/main_win.cpp)
else()
  add_executable(lumen src/main_sdl.cpp)
endif()
target_link_libraries(lumen PRIVATE lumen_engine)
set_target_properties(lumen PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# The runtime payload (Lua libraries, API stubs, docs, templates, shaders, fonts ...) travels with the executable in
# <exe dir>/runtime_data and is installed into lumen_runtime/ on the first launch, so a copied release folder is never empty.
add_custom_target(lumen_runtime_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${LUMEN_RUNTIME_DATA_DIR} $<TARGET_FILE_DIR:lumen>/runtime_data
  COMMENT "Copying runtime_data next to the executable")
add_dependencies(lumen lumen_runtime_data)

# The standalone player: the same runtime without the editor. `Export game` in the editor copies it next to data.lpak.
if(WIN32)
  add_executable(lumen-player WIN32 src/Player.cpp src/main_player_win.cpp)
else()
  add_executable(lumen-player src/Player.cpp src/main_player_sdl.cpp)
endif()
target_link_libraries(lumen-player PRIVATE lumen_engine)
target_include_directories(lumen-player PRIVATE ${CMAKE_SOURCE_DIR}/core/src ${LUMEN_LUA_INCLUDE_DIR})
set_target_properties(lumen-player PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(lumen lumen-player)
if(UNIX AND NOT APPLE)
  add_custom_command(TARGET lumen-player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LUMEN_WICKED_DIR}/WickedEngine/libdxcompiler.so $<TARGET_FILE_DIR:lumen-player>)
elseif(WIN32)
  add_custom_command(TARGET lumen-player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LUMEN_WICKED_DIR}/WickedEngine/dxcompiler.dll $<TARGET_FILE_DIR:lumen-player>)
endif()
if(TARGET lumen-lsp)
  add_dependencies(lumen lumen-lsp)
endif()
if(UNIX AND NOT APPLE)
  add_custom_command(TARGET lumen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LUMEN_WICKED_DIR}/WickedEngine/libdxcompiler.so $<TARGET_FILE_DIR:lumen>)
elseif(WIN32)
  add_custom_command(TARGET lumen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LUMEN_WICKED_DIR}/WickedEngine/dxcompiler.dll $<TARGET_FILE_DIR:lumen>)
endif()
