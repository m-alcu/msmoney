cmake_minimum_required(VERSION 3.16)
project(msmoney CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3 on this system is installed in /usr/local; its exported CMake config is
# broken, so locate it through pkg-config instead.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)

set(IMGUI_DIR src/vendor/imgui)
add_executable(msmoney
    src/main.cpp
    src/model.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(msmoney PRIVATE ${IMGUI_DIR})
target_link_libraries(msmoney PRIVATE SDL3::SDL3)
