cmake_minimum_required(VERSION 3.16)
project(BenchmarkPilhaA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Opcao para forcar OpenGL ES 2.0 ao compilar no Raspberry Pi 3
if(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    set(PLATFORM "Desktop" CACHE STRING "" FORCE)
    set(GRAPHICS "GRAPHICS_API_OPENGL_ES2" CACHE STRING "" FORCE)
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

# Procura a Raylib instalada no sistema; se nao achar (como no Windows), baixa automaticamente
find_package(raylib 5.0 QUIET)
if(NOT raylib_FOUND)
    message(STATUS "Baixando e configurando a Raylib 5.0 automaticamente via FetchContent...")
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.0
        GIT_SHALLOW TRUE
    )
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(raylib)
endif()

add_executable(benchmark_pilha_a
    src/main.cpp
)

target_include_directories(benchmark_pilha_a PRIVATE src)
target_link_libraries(benchmark_pilha_a PRIVATE raylib)

if(WIN32)
    target_link_libraries(benchmark_pilha_a PRIVATE psapi winmm)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(benchmark_pilha_a PRIVATE m pthread dl rt)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        # Raspberry Pi 3 em SO 64-bit (ARMv8 NEON nativo)
        target_compile_options(benchmark_pilha_a PRIVATE -O3 -mcpu=cortex-a53)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        # Raspberry Pi 3 em SO 32-bit (ARMv7/v8 com FPU NEON)
        target_compile_options(benchmark_pilha_a PRIVATE -O3 -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard)
    else()
        target_compile_options(benchmark_pilha_a PRIVATE -O3)
    endif()
endif()