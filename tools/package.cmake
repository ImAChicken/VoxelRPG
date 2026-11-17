# Makes the downloads: dist/VoxelRPG-Client.zip and dist/VoxelRPG-Server.zip
#
#   cmake -P tools/package.cmake          (from the repo root, after a Release build;
#                                          package.bat does both)
#
# Client: the game, data/, shaders/ and a README. Unzip anywhere and run VoxelRPG.
# Server: the server, data/ (no shaders), a README and a start script.
# Neither holds your worlds, settings made while playing, or anything from build/.
cmake_minimum_required(VERSION 3.20)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED BIN)
    foreach(d "${ROOT}/build/bin/Release" "${ROOT}/build/bin")
        if(EXISTS "${d}/VoxelRPG.exe" OR EXISTS "${d}/VoxelRPG")
            set(BIN "${d}")
            break()
        endif()
    endforeach()
endif()
if(NOT BIN)
    message(FATAL_ERROR "No built game found in build/bin/Release or build/bin. Build first (build.bat), or pass -DBIN=<folder>.")
endif()
set(EXE "")
if(EXISTS "${BIN}/VoxelRPG.exe")
    set(EXE ".exe")
endif()
foreach(f VoxelRPG VoxelRPG-Server)
    if(NOT EXISTS "${BIN}/${f}${EXE}")
        message(FATAL_ERROR "${BIN}/${f}${EXE} is missing: build both programs first.")
    endif()
endforeach()

set(DIST "${ROOT}/dist")
file(REMOVE_RECURSE "${DIST}/VoxelRPG-Client" "${DIST}/VoxelRPG-Server")
file(REMOVE "${DIST}/VoxelRPG-Client.zip" "${DIST}/VoxelRPG-Server.zip")
string(TIMESTAMP STAMP "%Y-%m-%d %H:%M")
file(STRINGS "${ROOT}/src/net/Protocol.h" PROTO_LINE REGEX "constexpr uint32_t VERSION")
string(REGEX MATCH "[0-9]+" PROTO "${PROTO_LINE}")
set(VERSION_TEXT "VoxelRPG build ${STAMP}\nnetwork version ${PROTO} (client and server must match)\n")

# ── Client ──
set(C "${DIST}/VoxelRPG-Client")
file(COPY "${BIN}/VoxelRPG${EXE}" DESTINATION "${C}")
file(COPY "${ROOT}/data" "${ROOT}/shaders" DESTINATION "${C}")
file(COPY "${ROOT}/tools/dist/README-Client.txt" DESTINATION "${C}")
file(RENAME "${C}/README-Client.txt" "${C}/README.txt")
file(WRITE "${C}/version.txt" "${VERSION_TEXT}")

# ── Server ──
set(S "${DIST}/VoxelRPG-Server")
file(COPY "${BIN}/VoxelRPG-Server${EXE}" DESTINATION "${S}")
file(COPY "${ROOT}/data" DESTINATION "${S}" PATTERN "resourcepacks" EXCLUDE PATTERN "controls" EXCLUDE)
file(COPY "${ROOT}/tools/dist/README-Server.txt" DESTINATION "${S}")
file(RENAME "${S}/README-Server.txt" "${S}/README.txt")
if(EXE)
    file(COPY "${ROOT}/tools/dist/Start-Server.bat" DESTINATION "${S}")
else()
    file(COPY "${ROOT}/tools/dist/start-server.sh" DESTINATION "${S}"
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
file(WRITE "${S}/version.txt" "${VERSION_TEXT}")

foreach(name VoxelRPG-Client VoxelRPG-Server)
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar cf "${DIST}/${name}.zip" --format=zip "${name}"
                    WORKING_DIRECTORY "${DIST}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Couldn't make ${name}.zip")
    endif()
    file(SIZE "${DIST}/${name}.zip" bytes)
    math(EXPR mb "${bytes} / 1048576")
    message(STATUS "dist/${name}.zip  (${mb} MB)")
endforeach()
message(STATUS "Done. Give friends VoxelRPG-Client.zip; VoxelRPG-Server.zip is for whoever hosts.")
