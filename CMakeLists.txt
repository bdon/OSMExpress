cmake_minimum_required (VERSION 3.5)
set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
project(OSMExpress)
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG -g")
set(CMAKE_CXX_FLAGS "-Wno-deprecated")
set(CMAKE_CXX_FLAGS "-Wno-deprecated-declarations")
set(CMAKE_CXX_FLAGS "-pthread")
set(OSMX_VERSION "0.2.0")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(ROARING_BUILD_STATIC ON CACHE INTERNAL "")
set(ROARING_LINK_STATIC ON CACHE INTERNAL "")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/capnproto EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/s2geometry EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/CRoaring EXCLUDE_FROM_ALL)

add_custom_target(build_lmdb COMMAND make liblmdb.a CC=clang XCFLAGS=-D_WANT_SEMUN
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lmdb/libraries/liblmdb
)

include_directories(vendor/libosmium/include)
include_directories(vendor/protozero/include)
include_directories(vendor/s2geometry/src)
include_directories(vendor/CRoaring/cpp)
include_directories(vendor/CRoaring/include)
include_directories(vendor/cxxopts/include)
include_directories(vendor/json/single_include)
include_directories(vendor/lmdb/libraries/liblmdb)
include_directories(vendor/capnproto/c++/src)
include_directories(include)

# needed for Expat install dir
if(CMAKE_SYSTEM_NAME STREQUAL FreeBSD)
include_directories(/usr/local/include)
link_directories(osmx /usr/local/lib)
endif()

add_executable(osmx src/cmd.cpp src/storage.cpp src/expand.cpp src/extract.cpp src/update.cpp src/region.cpp)
add_dependencies(osmx build_lmdb s2 kj capnp)

target_link_libraries(osmx z expat bz2 s2 roaring)
target_link_libraries(osmx ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lmdb/libraries/liblmdb/liblmdb.a)
target_link_libraries(osmx ${CMAKE_CURRENT_SOURCE_DIR}/vendor/capnproto/c++/src/capnp/libcapnp.a)
target_link_libraries(osmx ${CMAKE_CURRENT_SOURCE_DIR}/vendor/capnproto/c++/src/kj/libkj.a)

set_property(TARGET osmx PROPERTY CXX_STANDARD 14)
add_executable(osmxTest test/test_region.cpp src/region.cpp)
set_property(TARGET osmxTest PROPERTY CXX_STANDARD 14)
include_directories(include)
include_directories(vendor/Catch2/single_include)
target_link_libraries(osmxTest s2)
enable_testing()
add_test(osmxTest osmxTest)

add_executable (osmx-featuresource featuresource/main.cpp)
set_property(TARGET osmx-featuresource PROPERTY CXX_STANDARD 14)
include_directories(featuresource)
target_link_libraries(osmx-featuresource ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lmdb/libraries/liblmdb/liblmdb.a)
target_link_libraries(osmx-featuresource z bz2 s2 roaring osmx-static)
target_link_libraries(osmx-featuresource ${CMAKE_CURRENT_SOURCE_DIR}/vendor/capnproto/c++/src/capnp/libcapnp.a)
target_link_libraries(osmx-featuresource ${CMAKE_CURRENT_SOURCE_DIR}/vendor/capnproto/c++/src/kj/libkj.a)

install(TARGETS osmx DESTINATION bin)
add_custom_target(archive COMMAND dist/archive.sh ${OSMX_VERSION} ${CMAKE_SYSTEM_NAME})
add_dependencies(archive osmx)

add_library(osmx-static STATIC src/storage.cpp src/expand.cpp src/extract.cpp src/update.cpp src/region.cpp)
set_property(TARGET osmx-static PROPERTY CXX_STANDARD 14)

add_dependencies(osmx-static build_lmdb s2 kj capnp)
