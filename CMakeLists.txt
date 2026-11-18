cmake_minimum_required(VERSION 3.25)
project(VoxelRPG VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
    add_compile_options(/std:c++17 /bigobj)   # /bigobj: main.cpp has more sections than a plain .obj allows
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

# ozz-animation: skeletal animation clips (character/AnimClips.h).
# Only the runtime is built for the game; the glTF importer
# (gltf2ozz, to add new animation packs) with VOXELRPG_ANIM_TOOLS.
FetchContent_Declare(
    ozz
    GIT_REPOSITORY https://github.com/guillaumeblanc/ozz-animation.git
    GIT_TAG        0.16.0
)
option(VOXELRPG_ANIM_TOOLS "Build gltf2ozz, to convert new animation packs (tools/import_animations.md)" OFF)
set(ozz_build_tools ${VOXELRPG_ANIM_TOOLS} CACHE BOOL "" FORCE)
set(ozz_build_gltf ${VOXELRPG_ANIM_TOOLS} CACHE BOOL "" FORCE)
set(ozz_build_fbx OFF CACHE BOOL "" FORCE)
set(ozz_build_samples OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
set(ozz_build_tests OFF CACHE BOOL "" FORCE)
set(ozz_build_postfix OFF CACHE BOOL "" FORCE)
set(ozz_build_msvc_rt_dll OFF CACHE BOOL "" FORCE)   # static C runtime (/MT), like the game (line 4)

# ENet: UDP networking between the server and clients
# (net/EnetTransport.cpp is the only file that includes it)
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG        v1.3.18
)

FetchContent_MakeAvailable(glfw glm tomlplusplus ozz enet)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Threads — chunk generation and meshing run on worker threads
find_package(Threads REQUIRED)

# ── The core: the world and the simulation, no graphics ──
# Shared by the game, the headless server and the tests. Only
# sources that never touch OpenGL or the window go here.
set(CORE_SOURCES
    src/lz4/lz4.c
    src/noise/FastNoiseLite.cpp
    src/voxel/VoxelRegistry.cpp
    src/world/WorldSaveManage.cpp
    src/game/ItemRegistry.cpp
    src/game/StructureRegistry.cpp
    src/character/AnimClips.cpp
    src/chunk/ChunkLoader.cpp
    src/net/EnetTransport.cpp
)
add_library(voxelrpg_core STATIC ${CORE_SOURCES})
target_include_directories(voxelrpg_core PUBLIC src)
target_include_directories(voxelrpg_core PRIVATE ${enet_SOURCE_DIR}/include)
target_link_libraries(voxelrpg_core PRIVATE enet)
if(WIN32)
    target_link_libraries(voxelrpg_core PRIVATE ws2_32 winmm)
endif()
target_link_libraries(voxelrpg_core PUBLIC
    glm::glm
    Threads::Threads
    tomlplusplus::tomlplusplus
    ozz_animation
    ozz_base
)

# ── The game (with its own server for single player) ─────
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.c"
    "src/*.h"
)
foreach(f ${CORE_SOURCES})
    list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${f})
endforeach()
list(FILTER SOURCES EXCLUDE REGEX "src/server/ServerMain\\.cpp$")

add_executable(VoxelRPG ${SOURCES})

target_include_directories(VoxelRPG PRIVATE
    src
    src/glad/include
)

target_link_libraries(VoxelRPG PRIVATE
    voxelrpg_core
    glfw
    OpenGL::GL
)

# ── The dedicated server: no window, no graphics ─────────
#   VoxelRPG-Server [world] [--dev] [--port N]   (server/ServerMain.cpp)
add_executable(VoxelRPG-Server src/server/ServerMain.cpp)
target_link_libraries(VoxelRPG-Server PRIVATE voxelrpg_core)

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
    foreach(t voxelrpg_core VoxelRPG-Server)
        target_compile_options(${t} PRIVATE /MP /utf-8)
        target_compile_definitions(${t} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endforeach()
endif()

# Dev build flag
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(VoxelRPG PRIVATE DEV_BUILD)
endif()
# ── Tests ─────────────────────────────────────────────────
# Headless test programs (no window, no OpenGL). Off by default:
#   cmake -B build -DVOXELRPG_TESTS=ON
#   cmake --build build --config Release --target ragdoll_test woodcut_test combat_test sim_test npc_test anim_test inventory_test hit_test flat_test spawner_test chest_test icon_test build_test parts_test blueprint_test slope_test steer_test controls_test server_test net_test
#   build/bin/Release/ragdoll_test      (build/bin/ragdoll_test elsewhere)
#   build/bin/Release/woodcut_test      (from the repo root: it reads data/)
#   build/bin/Release/combat_test
#   build/bin/Release/sim_test          (from the repo root)
#   build/bin/Release/npc_test          (from the repo root)
#   build/bin/Release/anim_test         (from the repo root)
#   build/bin/Release/inventory_test    (from the repo root)
#   build/bin/Release/hit_test          (from the repo root)
#   build/bin/Release/flat_test         (from the repo root)
#   build/bin/Release/spawner_test      (from the repo root)
#   build/bin/Release/chest_test        (from the repo root)
#   build/bin/Release/icon_test         (from the repo root)
#   build/bin/Release/build_test        (from the repo root)
#   build/bin/Release/server_test       (from the repo root)
#   build/bin/Release/net_test          (from the repo root)
option(VOXELRPG_TESTS "Build the headless test programs in tests/" OFF)
if(VOXELRPG_TESTS)
    add_executable(ragdoll_test tests/ragdoll_test.cpp)
    # woodcut_test loads data/registry/*.toml: run it from the repo root
    add_executable(woodcut_test tests/woodcut_test.cpp)
    add_executable(combat_test tests/combat_test.cpp)
    add_executable(sim_test tests/sim_test.cpp)
    add_executable(npc_test tests/npc_test.cpp)
    add_executable(anim_test tests/anim_test.cpp)
    add_executable(inventory_test tests/inventory_test.cpp)
    add_executable(hit_test tests/hit_test.cpp)
    add_executable(flat_test tests/flat_test.cpp)
    add_executable(spawner_test tests/spawner_test.cpp)
    add_executable(chest_test tests/chest_test.cpp)
    add_executable(icon_test tests/icon_test.cpp)
    add_executable(build_test tests/build_test.cpp)
    add_executable(parts_test tests/parts_test.cpp)
    add_executable(blueprint_test tests/blueprint_test.cpp)
    add_executable(slope_test tests/slope_test.cpp)
    add_executable(steer_test tests/steer_test.cpp)
    add_executable(controls_test tests/controls_test.cpp)
    add_executable(server_test tests/server_test.cpp)
    add_executable(net_test tests/net_test.cpp)
    add_executable(net_bot tests/net_bot.cpp)      # a screenless player for trying a server (not a test)
    foreach(t ragdoll_test woodcut_test combat_test sim_test npc_test anim_test inventory_test hit_test flat_test spawner_test chest_test icon_test build_test parts_test blueprint_test slope_test steer_test controls_test server_test net_test net_bot)
        target_link_libraries(${t} PRIVATE voxelrpg_core)
        if(MSVC)
            target_compile_options(${t} PRIVATE /utf-8)
            target_compile_definitions(${t} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
        endif()
    endforeach()
endif()
