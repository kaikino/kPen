cmake_minimum_required(VERSION 3.10)
project(kPen)

set(CMAKE_CXX_STANDARD 17)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS}/SDL2)

# Dynamically find all .cc files in the src directory and its subdirectories
file(GLOB_RECURSE SRC_FILES "src/*.cc")

# Add the executable with the dynamically found source files
add_executable(kPen ${SRC_FILES})

# Include the src directory for header file resolution
target_include_directories(kPen PRIVATE src)

target_link_libraries(kPen SDL2::SDL2)
