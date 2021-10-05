# CMakeList.txt : CMake project for DatEngine, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.17)

project(DatArchive)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)

add_library(DatArchive INTERFACE)

target_include_directories(DatArchive INTERFACE .)

add_subdirectory(ZLIB)

target_link_libraries(DatArchive INTERFACE zlib)