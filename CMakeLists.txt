cmake_minimum_required(VERSION 3.10)
project(kPen)

set(CMAKE_CXX_STANDARD 17)

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS}/SDL2)

# Collect all .cc sources; add .mm only on Apple
file(GLOB_RECURSE SRC_FILES "src/*.cc")

if(APPLE)
    list(APPEND SRC_FILES "src/ClipboardMac.mm")
    add_executable(kPen MACOSX_BUNDLE ${SRC_FILES})
    set_target_properties(kPen PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    )
    target_link_libraries(kPen SDL2::SDL2 "-framework AppKit")

    # Bundle SDL2 dylib so the .app is self-contained
    get_target_property(SDL2_LIB SDL2::SDL2 LOCATION)
    add_custom_command(TARGET kPen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:kPen>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL2_LIB}"
            "$<TARGET_BUNDLE_DIR:kPen>/Contents/Frameworks/libSDL2.dylib"
        COMMAND install_name_tool -change
            "${SDL2_LIB}"
            "@executable_path/../Frameworks/libSDL2.dylib"
            "$<TARGET_FILE:kPen>"
        COMMENT "Bundling SDL2 into kPen.app"
    )
else()
    add_executable(kPen ${SRC_FILES})
    target_link_libraries(kPen SDL2::SDL2)
endif()

target_include_directories(kPen PRIVATE src)
