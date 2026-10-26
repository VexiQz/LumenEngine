cmake_minimum_required(VERSION 3.19)
project(LumenEngine VERSION 1.9.2 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

include(LumenOptions)
include(LumenDependencies)

add_subdirectory(core)

if(LUMEN_BUILD_ENGINE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/engine/CMakeLists.txt)
  add_subdirectory(engine)
endif()
if(LUMEN_BUILD_TOOLS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/CMakeLists.txt)
  add_subdirectory(tools)
endif()
if(LUMEN_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  enable_testing()
  add_subdirectory(tests)
endif()
