#------------------------------------------------------------------------------
# Ignition - iOS / iPadOS app
#------------------------------------------------------------------------------
#
#   cmake -S platforms/ios -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
#   cmake --build build-ios --config Release -- -sdk iphonesimulator
#   cmake --build build-ios --config Release -- -sdk iphoneos CODE_SIGNING_ALLOWED=NO
#
# `make ios-sim` and `make ios` wrap these; see README.md. Compiles
# ../../src directly: the engine and src/platform_ios.m (UIKit, OpenGL ES
# 3.0, GameController, RemoteIO). Only Apple's frameworks.

cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "Configure with -G Xcode -DCMAKE_SYSTEM_NAME=iOS")
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "")
project(ignition_ios C OBJC)

set(IGN_VERSION "0.0.0" CACHE STRING "This port's version, e.g. 0.1.0")
set(BUNDLE_ID "io.github.xesf.ignition" CACHE STRING "Bundle identifier")
set(DEVELOPMENT_TEAM "" CACHE STRING "Apple team ID for signed device builds")

set(APP_VERSION "0.0.0")
if(IGN_VERSION MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)")
    set(APP_VERSION ${CMAKE_MATCH_1})
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SRC ${ROOT}/src)
file(GLOB IGN_SOURCES ${SRC}/*.c ${SRC}/lisa/*.c)
list(FILTER IGN_SOURCES EXCLUDE REGEX "/platform_[a-z]+\\.c$")
list(FILTER IGN_SOURCES EXCLUDE REGEX "/rnd_gxm\\.c$")
list(APPEND IGN_SOURCES ${SRC}/platform_ios.m ${SRC}/platform_common.c ${SRC}/platform_touch.c)

add_executable(Ignition MACOSX_BUNDLE ${IGN_SOURCES})
set_property(TARGET Ignition PROPERTY C_STANDARD 99)
target_include_directories(Ignition PRIVATE ${SRC})
target_compile_definitions(Ignition PRIVATE
    IGN_GLES GLES_SILENCE_DEPRECATION _DEFAULT_SOURCE IGN_VERSION="${IGN_VERSION}")
# platform_ios.m has the app's main and runs main.c's under this name.
set_source_files_properties(${SRC}/main.c PROPERTIES COMPILE_DEFINITIONS main=ign_main)

# -fsigned-char: the game is x86 code, where char is signed.
target_compile_options(Ignition PRIVATE -fsigned-char
    -Wall -Wextra -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)

target_link_libraries(Ignition PRIVATE
    "-framework UIKit" "-framework OpenGLES" "-framework QuartzCore"
    "-framework GameController" "-framework AVFoundation" "-framework AudioToolbox"
    "-framework Foundation")

set_target_properties(Ignition PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${APP_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${APP_VERSION}
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID}
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 15.0
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL[variant=Release] 2
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DEVELOPMENT_TEAM}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
