cmake_minimum_required(VERSION 3.10)
project(kPen)

set(CMAKE_CXX_STANDARD 17)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS}/SDL2)

add_executable(kPen main.cc DrawingUtils.cc BrushTool.cc AbstractTool.cc BrushTool.cc SelectTool.cc ShapeTool.cc)

target_link_libraries(kPen SDL2::SDL2)
