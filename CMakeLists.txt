cmake_minimum_required(VERSION 3.25)
project(VoxelRPG VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
    add_compile_options(/std:c++17)
endif()

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output binaries here
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Fetch dependencies automatically
include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)

FetchContent_MakeAvailable(glfw glm tomlplusplus)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Threads — chunk generation and meshing run on worker threads
find_package(Threads REQUIRED)

# Collect all source files
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.c"
    "src/*.h"
)

add_executable(VoxelRPG ${SOURCES})

target_include_directories(VoxelRPG PRIVATE
    src
    src/glad/include
)

target_link_libraries(VoxelRPG PRIVATE
    glfw
    glm::glm
    OpenGL::GL
    Threads::Threads
    tomlplusplus::tomlplusplus
)

# Windows specific
if(MSVC)
    target_compile_options(VoxelRPG PRIVATE
        /W4
        /MP
        /utf-8
        /std:c++17
    )
    target_compile_definitions(VoxelRPG PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Dev build flag
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(VoxelRPG PRIVATE DEV_BUILD)
endif()
# ── Tests ─────────────────────────────────────────────────
# Headless test programs (no window, no OpenGL). Off by default:
#   cmake -B build -DVOXELRPG_TESTS=ON
#   cmake --build build --config Release --target ragdoll_test woodcut_test combat_test
#   build/bin/Release/ragdoll_test      (build/bin/ragdoll_test elsewhere)
#   build/bin/Release/woodcut_test      (from the repo root: it reads data/)
#   build/bin/Release/combat_test
option(VOXELRPG_TESTS "Build the headless test programs in tests/" OFF)
if(VOXELRPG_TESTS)
    add_executable(ragdoll_test tests/ragdoll_test.cpp)
    # woodcut_test loads data/registry/*.toml: run it from the repo root
    add_executable(woodcut_test tests/woodcut_test.cpp src/voxel/VoxelRegistry.cpp src/game/ItemRegistry.cpp)
    add_executable(combat_test tests/combat_test.cpp src/voxel/VoxelRegistry.cpp src/game/ItemRegistry.cpp)
    foreach(t ragdoll_test woodcut_test combat_test)
        target_include_directories(${t} PRIVATE src)
        target_link_libraries(${t} PRIVATE glm::glm tomlplusplus::tomlplusplus)
        if(MSVC)
            target_compile_options(${t} PRIVATE /utf-8)
            target_compile_definitions(${t} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
        endif()
    endforeach()
endif()
