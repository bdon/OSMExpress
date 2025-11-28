cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
project(OSMExpress)
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG -g")
set(CMAKE_CXX_FLAGS "-Wno-deprecated")
set(CMAKE_CXX_FLAGS "-Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS "-pthread")
set(CMAKE_CXX_STANDARD 23)
set(OSMX_VERSION "0.2.0")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(ROARING_USE_CPM OFF)
set(ENABLE_ROARING_TESTS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(FetchContent)

# TODO: Switch to a released version after next CapnProto release (post 1.2.0).
# Reason: https://github.com/capnproto/capnproto/issues/2353
# Change for v1: https://github.com/capnproto/capnproto/pull/2355
# Change for v2: https://github.com/capnproto/capnproto/pull/2354
FetchContent_Declare(
    CapnProto
    GIT_REPOSITORY https://github.com/capnproto/capnproto.git
    GIT_TAG master
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS 3)

FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG v3.3.1
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS)

FetchContent_Declare(
    LMDB
    GIT_REPOSITORY https://git.openldap.org/openldap/openldap.git
    GIT_TAG OPENLDAP_REL_ENG_2_6_10
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS)

FetchContent_Declare(
    Osmium
    GIT_REPOSITORY https://github.com/osmcode/libosmium.git
    GIT_TAG v2.22.0
    SOURCE_SUBDIR test/catch
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS)

FetchContent_Declare(
    Protozero
    GIT_REPOSITORY https://github.com/mapbox/protozero.git
    GIT_TAG v1.8.0
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS)

FetchContent_Declare(
    roaring
    GIT_REPOSITORY https://github.com/RoaringBitmap/CRoaring.git
    GIT_TAG v4.3.6
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS)

FetchContent_MakeAvailable(
    CapnProto Catch2 cxxopts LMDB nlohmann_json Osmium Protozero roaring)

if(NOT CapnProto_FOUND)
    add_subdirectory(${capnproto_SOURCE_DIR} EXCLUDE_FROM_ALL)
endif()

if(NOT TARGET LMDB::LMDB)
    set(LMDB_INCLUDE_DIR ${lmdb_SOURCE_DIR}/libraries/liblmdb)
    add_library(
        LMDB_LMDB
        STATIC
            ${lmdb_SOURCE_DIR}/libraries/liblmdb/mdb.c
            ${lmdb_SOURCE_DIR}/libraries/liblmdb/midl.c)
    target_include_directories(LMDB_LMDB PUBLIC ${LMDB_INCLUDE_DIR})

    add_library(LMDB::LMDB INTERFACE IMPORTED)
    set_target_properties(
        LMDB::LMDB
        PROPERTIES
            INTERFACE_LINK_LIBRARIES LMDB_LMDB
            INTERFACE_INCLUDE_DIRECTORIES ${LMDB_INCLUDE_DIR})
endif()

if(NOT OSMIUM_FOUND)
    add_library(Osmium INTERFACE)
    include_directories(SYSTEM ${osmium_SOURCE_DIR}/include)
endif()

if(NOT Protozero_FOUND)
    add_library(Protozero INTERFACE)
    include_directories(SYSTEM ${protozero_SOURCE_DIR}/include)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/s2geometry EXCLUDE_FROM_ALL)

include_directories(vendor/s2geometry/src)
include_directories(include)

# needed for Expat install dir
if(CMAKE_SYSTEM_NAME STREQUAL FreeBSD)
include_directories(/usr/local/include)
link_directories(osmx /usr/local/lib)
endif()

set(CAPNPC_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/capnpc_generated)
file(MAKE_DIRECTORY ${CAPNPC_OUTPUT_DIR})
capnp_generate_cpp(CAPNP_SRCS CAPNP_HDRS include/osmx/messages.capnp)

add_executable(
    osmx
    src/cmd.cpp
    src/storage.cpp
    src/expand.cpp
    src/extract.cpp
    src/update.cpp
    src/region.cpp
    ${CAPNP_SRCS})

add_dependencies(osmx s2)

target_include_directories(
    osmx
    PUBLIC include ${CAPNPC_OUTPUT_DIR}/include)

target_link_libraries(
    osmx
    bz2 CapnProto::capnp cxxopts::cxxopts expat LMDB::LMDB
    nlohmann_json::nlohmann_json roaring s2 z)

add_executable(osmxTest test/test_region.cpp src/region.cpp)

target_include_directories(
    osmxTest
    PUBLIC include ${CAPNPC_OUTPUT_DIR}/include)

target_link_libraries(
    osmxTest
    bz2 CapnProto::capnp cxxopts::cxxopts expat LMDB::LMDB
    nlohmann_json::nlohmann_json roaring s2 z
    Catch2::Catch2WithMain)

enable_testing()
add_test(osmxTest osmxTest)

install(TARGETS osmx DESTINATION bin)
add_custom_target(archive COMMAND dist/archive.sh ${OSMX_VERSION} ${CMAKE_SYSTEM_NAME})
add_dependencies(archive osmx)

add_library(
    osmx-static
    STATIC
    src/storage.cpp
    src/expand.cpp
    src/extract.cpp
    src/update.cpp
    src/region.cpp)

target_include_directories(
    osmx-static
    PUBLIC include ${CAPNPC_OUTPUT_DIR}/include)

target_link_libraries(
    osmx-static
    bz2 CapnProto::capnp cxxopts::cxxopts expat LMDB::LMDB
    nlohmann_json::nlohmann_json roaring s2 z)
