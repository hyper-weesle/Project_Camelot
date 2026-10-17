# Sonic and the Black Knight PC - launcher (SDL3 + Dear ImGui, statically linked).
#   cmake -S satbk/launcher -B build/launcher -DSDL3_SOURCE_DIR=... -DSDL3_LIBRARY=... -DIMGUI_DIR=...
cmake_minimum_required(VERSION 3.20)
project(SatbkLauncher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR "" CACHE PATH "Dear ImGui source directory")
set(SDL3_INCLUDE_DIRS "" CACHE STRING "SDL3 include directories (;-separated)")
set(SDL3_LIBRARY "" CACHE FILEPATH "Static SDL3 library")
set(LAUNCHER_FONT "${IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf" CACHE FILEPATH "UI font (TTF)")

# Embed the UI font so the launcher is a single file.
set(FONT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/launcher_font.h")
add_custom_command(
    OUTPUT "${FONT_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${LAUNCHER_FONT} -DOUTPUT=${FONT_HEADER} -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake
    DEPENDS "${LAUNCHER_FONT}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_font.cmake")

add_executable(SonicBlackKnightLauncher WIN32
    launcher.cpp config_file.cpp "${FONT_HEADER}"
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
if(WIN32)
    target_sources(SonicBlackKnightLauncher PRIVATE launcher.rc)
endif()
target_include_directories(SonicBlackKnightLauncher PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}" "${IMGUI_DIR}" "${IMGUI_DIR}/backends" ${SDL3_INCLUDE_DIRS})
target_link_libraries(SonicBlackKnightLauncher PRIVATE "${SDL3_LIBRARY}")
if(WIN32)
    target_link_libraries(SonicBlackKnightLauncher PRIVATE
        setupapi winmm imm32 version ole32 oleaut32 gdi32 user32 shell32 advapi32 uuid cfgmgr32)
    target_link_options(SonicBlackKnightLauncher PRIVATE -static)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(SonicBlackKnightLauncher PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
