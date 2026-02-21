cmake_minimum_required(VERSION 3.10)
project(kPen)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 REQUIRED)

add_executable(kPen main.cc)

target_link_libraries(kPen SDL2::SDL2)