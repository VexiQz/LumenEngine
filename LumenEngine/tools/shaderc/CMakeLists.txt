add_executable(lumen-shaderc main.cpp)
target_link_libraries(lumen-shaderc PRIVATE lumen_core WickedEngine)
set_target_properties(lumen-shaderc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(UNIX AND NOT APPLE)
  add_custom_command(TARGET lumen-shaderc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LUMEN_WICKED_DIR}/WickedEngine/libdxcompiler.so $<TARGET_FILE_DIR:lumen-shaderc>)
elseif(WIN32)
  add_custom_command(TARGET lumen-shaderc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LUMEN_WICKED_DIR}/WickedEngine/dxcompiler.dll $<TARGET_FILE_DIR:lumen-shaderc>)
endif()
