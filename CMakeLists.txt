# CMakeList.txt : CMake project for DatEngine, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.17)

if (${CMAKE_CXX_STANDARD} < 17)
    message(FATAL_ERROR "This library requires c++ 17 to work")
endif()
target_include_directories(${PROJECT_NAME} PUBLIC .)

add_subdirectory(ZLib)