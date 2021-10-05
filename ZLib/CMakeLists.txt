cmake_minimum_required (VERSION 3.17)

project(zlib)

add_library(zlib INTERFACE)

target_include_directories(zlib INTERFACE include)

if (UNIX)
    target_include_directories(zlib INTERFACE unix/include)
    target_link_directories(zlib INTERFACE unix)
    target_link_libraries(zlib INTERFACE libz)
else()
    target_include_directories(zlib INTERFACE win/include)
    target_link_directories(zlib INTERFACE win)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        message (STATUS "Compiling for 64-bit")
        target_link_libraries(zlib INTERFACE zlibstatic)
    else()
        message(STATUS "Compiling for 32-bit")
        target_link_libraries(zlib INTERFACE zlibstatic32)
    endif()
endif()