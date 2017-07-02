project(vfs.rar)

cmake_minimum_required(VERSION 2.6)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(Kodi REQUIRED)
find_package(p8-platform REQUIRED)
find_package(kodiplatform REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${kodiplatform_INCLUDE_DIRS}
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
                -D__STDC_FORMAT_MACROS)

set(DEPLIBS unrarxlib ${p8-platform_LIBRARIES} ${kodiplatform_LIBRARIES})

build_addon(vfs.rar RAR DEPLIBS)
