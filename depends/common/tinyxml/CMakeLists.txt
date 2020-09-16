cmake_minimum_required(VERSION 3.1)
project(tinyxml)

set(SOURCES src/tinystr.cpp
            src/tinyxml.cpp
            src/tinyxmlerror.cpp
            src/tinyxmlparser.cpp)

if(WIN32)
  add_definitions(-DWIN32 -D_LIB)
endif()
add_definitions(-DTIXML_USE_STL)

add_library(tinyxml ${SOURCES})

include_directories(${PROJECT_SOURCE_DIR}/include)

set(HEADERS ${PROJECT_SOURCE_DIR}/include/tinystr.h
            ${PROJECT_SOURCE_DIR}/include/tinyxml.h)

install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_PREFIX}/include)
install(TARGETS tinyxml DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)

# As this static no need to make it public
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()
