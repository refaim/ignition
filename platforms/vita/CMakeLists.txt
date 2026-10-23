#------------------------------------------------------------------------------
# Ignition - PlayStation Vita build
#------------------------------------------------------------------------------
#
#   tools/vita_deps.sh build-vita/deps
#   cmake -S platforms/vita -B build-vita -DVITA_DEPS=build-vita/deps
#   cmake --build build-vita
#       -> build-vita/ignition.vpk (title IGNT00001)
#
# Or "make vita" from the top, which does all three. Needs VitaSDK
# ($VITASDK, or ~/vitasdk) with vitaShaRK, libmathneon, taihen and
# SceShaccCgExt installed (vdpm), and vitaGL from tools/vita_deps.sh.
# Compiles ../../src directly; the Vita parts are src/platform_vita.c and
# src/gl_compat.c. See the README.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{VITASDK} AND EXISTS "$ENV{HOME}/vitasdk")
    set(ENV{VITASDK} "$ENV{HOME}/vitasdk")
endif()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "VITASDK is not set: install VitaSDK and export VITASDK")
    endif()
endif()

project(ignition_vita C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SRC ${ROOT}/src)
set(VITA_DEPS "" CACHE PATH "absolute prefix with include/ and lib/ for vitaGL (tools/vita_deps.sh)")

# Every engine file, and the Vita host instead of the others.
file(GLOB IGN_SOURCES ${SRC}/*.c ${SRC}/lisa/*.c)
list(FILTER IGN_SOURCES EXCLUDE REGEX "/platform_[a-z]+\\.c$")
list(APPEND IGN_SOURCES ${SRC}/platform_vita.c)

add_executable(ignition ${IGN_SOURCES})
target_include_directories(ignition PRIVATE ${SRC})
if(VITA_DEPS)
    target_include_directories(ignition BEFORE PRIVATE ${VITA_DEPS}/include)
    target_link_directories(ignition BEFORE PRIVATE ${VITA_DEPS}/lib)
endif()
# -fsigned-char: the game is x86 code, where char is signed.
target_compile_options(ignition PRIVATE
    -O2 -fsigned-char -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation)
# Room after the code for the module info vita-elf-create appends (the
# SDK's linker script reads __sce_headroom).
target_link_options(ignition PRIVATE -Wl,--defsym,__sce_headroom=0x4000)
target_link_libraries(ignition
    vitaGL
    vitashark
    SceShaccCgExt
    mathneon
    taihen_stub
    SceShaccCg_stub
    SceKernelDmacMgr_stub
    SceGxm_stub
    SceDisplay_stub
    SceCommonDialog_stub
    SceAppMgr_stub
    SceSysmodule_stub
    SceCtrl_stub
    SceAudio_stub
    ScePower_stub
    SceIofilemgr_stub
    SceLibKernel_stub
    stdc++
    m
    c
)

# LiveArea art, drawn by tools/vita_sce_sys.py.
set(SYS ${CMAKE_CURRENT_BINARY_DIR}/sce_sys)
add_custom_command(
    OUTPUT ${SYS}/icon0.png ${SYS}/livearea/contents/bg.png ${SYS}/livearea/contents/startup.png
    COMMAND python3 ${ROOT}/tools/vita_sce_sys.py ${SYS}
    DEPENDS ${ROOT}/tools/vita_sce_sys.py
    COMMENT "LiveArea art")
add_custom_target(ignition_sce_sys DEPENDS ${SYS}/icon0.png)

vita_create_self(eboot.bin ignition UNSAFE)
vita_create_vpk(ignition.vpk IGNT00001 eboot.bin
    VERSION 01.00
    NAME "Ignition"
    FILE ${SYS}/icon0.png sce_sys/icon0.png
         ${SYS}/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
         ${SYS}/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
         ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
add_dependencies(ignition.vpk-vpk ignition_sce_sys)
