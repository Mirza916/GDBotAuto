cmake_minimum_required(VERSION 3.16)
project(GDAutoBot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- shared, platform independent bot core ----
add_library(gdbot_core STATIC
    core/src/common.cpp
    core/src/macro.cpp
    core/src/vision.cpp
    core/src/pilot.cpp
    core/src/engine.cpp)
target_include_directories(gdbot_core PUBLIC core/include)

option(GDBOT_TESTS "Build host tests + simulator" ON)
if(GDBOT_TESTS AND NOT WIN32 AND NOT ANDROID)
    add_executable(gdbot_tests tests/test_main.cpp tests/sim.cpp)
    target_link_libraries(gdbot_tests gdbot_core)
endif()

# ---- Windows app (build on Windows with MSVC/MinGW, or cross-compile, see BUILD.md) ----
if(WIN32)
    add_executable(GDAutoBot WIN32 windows/src/main.cpp windows/src/win_platform.cpp windows/resources/app.rc)
    target_link_libraries(GDAutoBot gdbot_core user32 gdi32 winmm shell32 comdlg32 dwmapi)
    target_compile_definitions(GDAutoBot PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MINGW)
        target_link_options(GDAutoBot PRIVATE -static -municode)
    endif()
    if(MSVC)
        # app.rc already embeds windows/resources/app.manifest
        target_link_options(GDAutoBot PRIVATE /MANIFEST:NO)
        target_compile_options(GDAutoBot PRIVATE /utf-8)
        target_compile_options(gdbot_core PRIVATE /utf-8)
    endif()
endif()
