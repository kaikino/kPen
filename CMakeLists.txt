cmake_minimum_required(VERSION 3.10)
project(kPen)

set(CMAKE_CXX_STANDARD 17)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS}/SDL2)

# Dynamically find all .cc and .mm files in src and subdirectories
file(GLOB_RECURSE SRC_FILES "src/*.cc" "src/*.mm")

# Add the executable with the dynamically found source files
add_executable(kPen ${SRC_FILES})

# Include the src directory for header file resolution
target_include_directories(kPen PRIVATE src)

target_link_libraries(kPen SDL2::SDL2)

if(APPLE)
    target_link_libraries(kPen "-framework AppKit")
    # Set .mm files to compile as Objective-C++ explicitly
    file(GLOB_RECURSE MM_FILES "src/*.mm")
    set_source_files_properties(${MM_FILES} PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )
endif()
