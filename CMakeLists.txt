cmake_minimum_required(VERSION 3.5)
project(vfs.rar)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(TinyXML REQUIRED)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <codecvt>
                           int main() { return 0; }" HAS_CODECVT)
if(HAS_CODECVT)
  add_definitions(-DHAS_CODECVT)
endif()

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${TINYXML_INCLUDE_DIRS}
                    ${PROJECT_SOURCE_DIR}/lib/UnrarXLib)

add_subdirectory(lib/UnrarXLib)

set(RAR_SOURCES src/RarControl.cpp
                src/RarFile.cpp
                src/RarExtractThread.cpp
                src/RarManager.cpp)

set(RAR_HEADERS src/Helpers.h
                src/RarControl.h
                src/RarExtractThread.h
                src/RarManager.h)

add_definitions(-D_LARGEFILE64_SOURCE
                -D_FILE_OFFSET_BITS=64
                -D__STDC_FORMAT_MACROS
                -D_CRT_NONSTDC_NO_DEPRECATE
                -DRARDLL
                -DSILENT)

set(DEPLIBS unrarxlib ${TINYXML_LIBRARIES})

build_addon(vfs.rar RAR DEPLIBS)

include(CPack)
