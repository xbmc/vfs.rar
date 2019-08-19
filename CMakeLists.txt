cmake_minimum_required(VERSION 3.5)
project(vfs.rar)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(p8-platform REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${p8-platform_INCLUDE_DIRS}
                    ${PROJECT_SOURCE_DIR}/lib/UnrarXLib)

add_subdirectory(lib/UnrarXLib)

set(RAR_SOURCES src/RarFile.cpp
                src/RarExtractThread.cpp
                src/RarManager.cpp)

set(RAR_HEADERS src/RarExtractThread.h
                src/RarManager.h)

add_definitions(-D_LARGEFILE64_SOURCE
                -D_FILE_OFFSET_BITS=64
                -D__STDC_FORMAT_MACROS
                -D_CRT_NONSTDC_NO_DEPRECATE)

set(DEPLIBS unrarxlib ${p8-platform_LIBRARIES})

build_addon(vfs.rar RAR DEPLIBS)

include(CPack)
