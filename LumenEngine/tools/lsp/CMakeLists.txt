add_executable(lumen-lsp main.cpp)
target_link_libraries(lumen-lsp PRIVATE lumen_core)
set_target_properties(lumen-lsp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
