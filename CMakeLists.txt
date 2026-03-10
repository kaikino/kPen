cmake_minimum_required(VERSION 3.10)
project(kPen)

set(CMAKE_CXX_STANDARD 17)

# App version from Git: use latest tag by version number (e.g. v1.1.1) so it updates after tagging.
# Avoid OUTPUT_VARIABLE names that might collide with CMake internals (e.g. GIT_TAG).
set(KPEN_VERSION_STRING "0.0.0")
if(UNIX)
    # Latest tag by semantic version (v1.0.0, v1.0.6, v1.1.1 -> v1.1.1)
    execute_process(
        COMMAND sh -c "git tag -l 'v*' 2>/dev/null | sort -V | tail -1"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE KPEN_GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    execute_process(
        COMMAND git describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE KPEN_GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(KPEN_GIT_TAG MATCHES "^v(.+)$")
    set(KPEN_VERSION_STRING "${CMAKE_MATCH_1}")
elseif(KPEN_GIT_TAG)
    set(KPEN_VERSION_STRING "${KPEN_GIT_TAG}")
endif()
message(STATUS "kPen version: ${KPEN_VERSION_STRING}")
configure_file(${CMAKE_SOURCE_DIR}/src/version.h.in ${CMAKE_BINARY_DIR}/version.h)
if(APPLE)
    configure_file(${CMAKE_SOURCE_DIR}/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS}/SDL2)

# Collect all .cc sources; add .mm only on Apple
file(GLOB_RECURSE SRC_FILES "src/*.cc")

list(APPEND SRC_FILES "src/menu/tinyfiledialogs.c")

if(APPLE)
    list(APPEND SRC_FILES "src/menu/Macmenu.mm")
    list(APPEND SRC_FILES "src/stb/ClipboardMac.mm")
    add_executable(kPen MACOSX_BUNDLE ${SRC_FILES})
    set_target_properties(kPen PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
    )
    target_link_libraries(kPen SDL2::SDL2 "-framework AppKit")

    # Bundle SDL2 dylib so the .app is self-contained.
    # We copy the dylib into Frameworks/ and retarget the binary to load it from
    # @executable_path rather than the Homebrew prefix, so the .app runs on any Mac.
    get_target_property(SDL2_LIB SDL2::SDL2 LOCATION)
    get_filename_component(SDL2_DYLIB_NAME "${SDL2_LIB}" NAME)

    add_custom_command(TARGET kPen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:kPen>/Contents/Frameworks"
        # Copy the actual dylib (may have a version suffix like libSDL2-2.0.0.dylib)
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL2_LIB}"
            "$<TARGET_BUNDLE_DIR:kPen>/Contents/Frameworks/${SDL2_DYLIB_NAME}"
        # Retarget the binary to load SDL2 from the bundled copy
        COMMAND install_name_tool -change
            "${SDL2_LIB}"
            "@executable_path/../Frameworks/${SDL2_DYLIB_NAME}"
            "$<TARGET_FILE:kPen>"
        COMMENT "Bundling SDL2 (${SDL2_DYLIB_NAME}) into kPen.app"
    )
    # App icon
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/AppIcon.icns")
        add_custom_command(TARGET kPen POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:kPen>/Contents/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/assets/AppIcon.icns"
                "$<TARGET_BUNDLE_DIR:kPen>/Contents/Resources/AppIcon.icns"
            COMMENT "Copying AppIcon.icns into kPen.app"
        )
    endif()
else()
    add_executable(kPen ${SRC_FILES})
    
    if(WIN32)
        target_link_libraries(kPen PRIVATE 
            $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
            $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        )
    else()
        target_link_libraries(kPen SDL2::SDL2)
    endif()
endif()

target_include_directories(kPen PRIVATE src ${CMAKE_BINARY_DIR})
