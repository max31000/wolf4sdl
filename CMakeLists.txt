# Emscripten (WebAssembly) build of Wolf4SDL. Native builds keep using the Makefile /
# Visual Studio projects. Contract of the web module: web/WEB.md.
#
#   emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-web            -> build-web/bin/wolf3d.{js,wasm}
cmake_minimum_required(VERSION 3.16)
project(wolf4sdl C)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "CMakeLists.txt only builds the Emscripten web version; use the Makefile for native builds.")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Game version (see version.h). Default: Wolfenstein 3D shareware v1.4 (*.WL1).
set(WOLF_VERSION_DEFINES "CARMACIZED;UPLOAD" CACHE STRING "version.h defines of the game version to build")
# Dev only: data directory to --preload-file into /data (release builds carry no game data).
set(WOLF_WEB_PRELOAD_DIR "" CACHE PATH "Dev only: game data directory to --preload-file into /data")

set(SOURCES
    mame/fmopl.c
    id_ca.c id_in.c id_pm.c id_sd.c id_us.c id_vw.c
    signon.c
    wl_act1.c wl_act2.c wl_agent.c wl_atmos.c wl_cloudsky.c wl_debug.c wl_draw.c wl_game.c
    wl_inter.c wl_main.c wl_menu.c wl_parallax.c wl_plane.c wl_play.c wl_scale.c wl_shade.c
    wl_state.c wl_text.c wl_utils.c
    web/bliss_web.c
)

set(PORT_FLAGS -sUSE_SDL=2 -sUSE_SDL_MIXER=2 "-sSDL2_MIXER_FORMATS=[]")

add_executable(wolf3d ${SOURCES})
set_target_properties(wolf3d PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    C_STANDARD 99
    C_EXTENSIONS ON)
target_include_directories(wolf3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# CLASSIC_MENU: the original menus and default controls (no free look / WASD / display menu).
target_compile_definitions(wolf3d PRIVATE VERSIONALREADYCHOSEN ${WOLF_VERSION_DEFINES} CLASSIC_MENU)
target_compile_options(wolf3d PRIVATE ${PORT_FLAGS} -O3 -Wno-pointer-sign -Wno-parentheses
    -Wno-dangling-else -Wno-pragma-pack -Wno-format -Wno-unused-value -Wno-incompatible-pointer-types-discards-qualifiers)

set(WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/web)
set(WEB_EXPORTS "_main,_bliss_pause,_bliss_resume,_bliss_is_paused,_bliss_set_master_volume,_bliss_get_master_volume,_bliss_get_frame_count,_bliss_get_frame_work,_bliss_dispose,_bliss_release_keys")
target_link_options(wolf3d PRIVATE
    ${PORT_FLAGS} -O3
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH=1
    # The game runs its original blocking loops on a fiber (web/bliss_web.c).
    -sASYNCIFY=1
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createWolf3d
    "-sEXPORTED_FUNCTIONS=${WEB_EXPORTS}"
    "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,FS,addRunDependency,removeRunDependency"
    --pre-js ${WEB_DIR}/pre.js
    --js-library ${WEB_DIR}/library_bliss.js)
if(WOLF_WEB_PRELOAD_DIR)
    message(STATUS "Dev build: preloading ${WOLF_WEB_PRELOAD_DIR} into /data")
    target_link_options(wolf3d PRIVATE "--preload-file" "${WOLF_WEB_PRELOAD_DIR}@/data")
endif()
set_property(TARGET wolf3d APPEND PROPERTY LINK_DEPENDS "${WEB_DIR}/pre.js;${WEB_DIR}/library_bliss.js")
