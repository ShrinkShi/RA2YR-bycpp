cmake_minimum_required(VERSION 3.28)

project(RA2YRByCpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "The first playable client slice currently targets Windows/D3D11.")
endif()

find_package(SDL3 CONFIG REQUIRED)

add_library(ra2yr_core
    src/Engine/Core/Types.h
    src/Westwood/Ini/Ini.cpp
    src/Westwood/Ini/Ini.h
    src/Westwood/Palette/Palette.cpp
    src/Westwood/Palette/Palette.h
    src/Westwood/Shp/Shp.cpp
    src/Westwood/Shp/Shp.h
    src/GameData/Rules.cpp
    src/GameData/Rules.h
    src/Simulation/Simulation.cpp
    src/Simulation/Simulation.h
)

target_include_directories(ra2yr_core PUBLIC src)
target_compile_definitions(ra2yr_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(ra2yr_core PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(ra2yr_core PRIVATE /W4 /WX)
endif()

add_executable(ra2yr_client
    src/Client/main.cpp
    src/Renderer/D3D11Renderer.cpp
    src/Renderer/D3D11Renderer.h
)

target_link_libraries(ra2yr_client PRIVATE ra2yr_core SDL3::SDL3 d3d11 dxgi d3dcompiler d2d1 dwrite gdi32 user32)
target_compile_definitions(ra2yr_client PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SDL_MAIN_HANDLED)

if(MSVC)
    target_compile_options(ra2yr_client PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(ra2yr_client PRIVATE /W4 /WX)
endif()

enable_testing()
add_executable(ra2yr_tests
    tests/core_tests.cpp
)
target_link_libraries(ra2yr_tests PRIVATE ra2yr_core)
target_compile_definitions(ra2yr_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(ra2yr_tests PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(ra2yr_tests PRIVATE /W4 /WX)
endif()
add_test(NAME ra2yr_core_tests COMMAND ra2yr_tests)

add_custom_command(TARGET ra2yr_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ra2yr_client>/assets/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui/strings.ini"
        "$<TARGET_FILE_DIR:ra2yr_client>/assets/ui/strings.ini"
)
