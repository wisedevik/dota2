cmake_minimum_required(VERSION 3.20)
project(dota_private_server C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(protobuf CONFIG REQUIRED)   # Homebrew protobuf, pulls in abseil
find_package(Threads REQUIRED)

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(CLIENT_BIN_DIR ${CMAKE_SOURCE_DIR}/client/game/game/bin/osx64
    CACHE PATH "The game client's bin/osx64 (where libsteam_api.dylib lives)")

# Everything includes relative to src/ and src/public/, like a Valve tree.
function(dota_target_includes target)
  target_include_directories(${target} PUBLIC ${SRC_DIR} ${SRC_DIR}/public)
endfunction()

set(APPLE_CRYPTO_LIBS "-framework Security" "-framework CoreFoundation" z)

# --- protobufs: generated at build time from protobufs/dota2 --------------------
# Imports resolve to the protobuf library's own descriptor.proto first.
file(GLOB DOTA2_PROTOS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/protobufs/dota2/*.proto)
get_target_property(PROTOBUF_INCLUDE_DIRS protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/protobufs)
add_library(dota2_protobufs STATIC ${DOTA2_PROTOS})
protobuf_generate(TARGET dota2_protobufs LANGUAGE cpp
  IMPORT_DIRS ${PROTOBUF_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/protobufs/dota2
  PROTOC_OUT_DIR ${CMAKE_BINARY_DIR}/protobufs)
target_include_directories(dota2_protobufs PUBLIC ${CMAKE_BINARY_DIR}/protobufs)
target_link_libraries(dota2_protobufs PUBLIC protobuf::libprotobuf)
# Valve's proto2 leans on deprecated / large-switch patterns; quiet the noise.
target_compile_options(dota2_protobufs PRIVATE -Wno-deprecated-declarations)

# --- steamcm: the Steam CM protocol (protobuf-free) --------------------------------
# The client half is also compiled into the x86_64 steam_api below.
set(STEAMCM_CLIENT_SOURCES
  ${SRC_DIR}/steamcm/cmclient.cpp
  ${SRC_DIR}/steamcm/cmconnection.cpp
  ${SRC_DIR}/steamcm/cmcrypto.cpp
  ${SRC_DIR}/steamcm/cmmsg.cpp)
add_library(steamcm STATIC ${STEAMCM_CLIENT_SOURCES} ${SRC_DIR}/steamcm/cmserver.cpp)
dota_target_includes(steamcm)
target_link_libraries(steamcm PUBLIC ${APPLE_CRYPTO_LIBS} Threads::Threads)

# --- gcsdk: GC messages and shared objects ----------------------------------------
add_library(gcsdk STATIC
  ${SRC_DIR}/gcsdk/gcmsg.cpp
  ${SRC_DIR}/gcsdk/sharedobjectcache.cpp)
dota_target_includes(gcsdk)
target_link_libraries(gcsdk PUBLIC dota2_protobufs)

# --- dota_gc: the Dota Game Coordinator ---------------------------------------------
add_library(dota_gc STATIC
  ${SRC_DIR}/dota_gc/dotagcserver.cpp
  ${SRC_DIR}/dota_gc/dotalobbymanager.cpp)
dota_target_includes(dota_gc)
target_link_libraries(dota_gc PUBLIC gcsdk)

# --- executables ----------------------------------------------------------------------
add_executable(gcserver ${SRC_DIR}/gcserver/main.cpp)
target_link_libraries(gcserver PRIVATE dota_gc steamcm)

# --- tests ---------------------------------------------------------------------------
enable_testing()
add_executable(steamcm_test ${SRC_DIR}/unittests/steamcm_test.cpp)
target_link_libraries(steamcm_test PRIVATE dota_gc steamcm)
add_test(NAME steamcm_test COMMAND steamcm_test)

# --- client-side libraries (x86_64, the client runs under Rosetta) ----------------------
# Built into build/ and copied into the client's bin/osx64 after each build.
option(DOTA_BUILD_CLIENT_LIBS "Build and install steam_api and window_fix into the client" ON)
if(DOTA_BUILD_CLIENT_LIBS AND EXISTS ${CLIENT_BIN_DIR}/libtier0.dylib)
  function(dota_install_into_client target file_name)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${CLIENT_BIN_DIR}/${file_name}
      COMMAND codesign --force --sign - ${CLIENT_BIN_DIR}/${file_name}
      COMMENT "Installing ${file_name} into ${CLIENT_BIN_DIR}")
  endfunction()

  # libsteam_api.dylib replacement. The shipped original is kept next to it as
  # libsteam_api.dylib.orig.
  add_library(steam_api SHARED
    ${SRC_DIR}/steam_api/steam_api.cpp
    ${SRC_DIR}/steam_api/steamapiconfig.cpp
    ${SRC_DIR}/steam_api/steamcallbacks.cpp
    ${SRC_DIR}/steam_api/steamclient.cpp
    ${SRC_DIR}/steam_api/steamgamecoordinator.cpp
    ${STEAMCM_CLIENT_SOURCES})
  dota_target_includes(steam_api)
  target_link_libraries(steam_api PRIVATE ${APPLE_CRYPTO_LIBS})
  set_target_properties(steam_api PROPERTIES
    OSX_ARCHITECTURES x86_64
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    INSTALL_NAME_DIR "@loader_path"
    BUILD_WITH_INSTALL_NAME_DIR ON)
  dota_install_into_client(steam_api libsteam_api.dylib)

  # window_fix.dylib, interposes libtier0's CThreadSyncObject::Wait.
  add_library(window_fix SHARED ${SRC_DIR}/windowfix/windowfix.c)
  target_link_libraries(window_fix PRIVATE "-framework CoreFoundation" ${CLIENT_BIN_DIR}/libtier0.dylib)
  set_target_properties(window_fix PROPERTIES
    OSX_ARCHITECTURES x86_64
    PREFIX ""
    INSTALL_NAME_DIR "@loader_path"
    BUILD_WITH_INSTALL_NAME_DIR ON)
  dota_install_into_client(window_fix window_fix.dylib)
endif()
