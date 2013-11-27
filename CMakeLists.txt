project(vfs.rar)

cmake_minimum_required(VERSION 2.6)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(xbmc REQUIRED)
find_package(xbmcplatform REQUIRED)

include_directories(${XBMC_INCLUDE_DIR}
                    ${XBMCPLATFORM_INCLUDE_DIRS}
                    ${PROJECT_SOURCE_DIR}/lib/UnrarXLib)

add_subdirectory(lib/UnrarXLib)

set(RAR_SOURCES src/RarFile.cpp
                src/RarExtractThread.cpp
                src/RarManager.cpp)

add_definitions(-D_LARGEFILE64_SOURCE
                -D_FILE_OFFSET_BITS=64
                -D__STDC_FORMAT_MACROS)

set(DEPLIBS unrarxlib)

build_addon(vfs.rar RAR DEPLIBS)
