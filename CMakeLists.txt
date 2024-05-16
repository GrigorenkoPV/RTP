cmake_minimum_required(VERSION 3.22)
project(raidtestbed)

set(CMAKE_CXX_STANDARD 20)


# Diagnostics
add_compile_options(-Wall)
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-ferror-limit=0)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fmax-errors=0)
endif ()


add_compile_options(-mavx)
add_compile_options(-O3 -finline)

include_directories(./Include ./confuse)

add_executable(testbed
        disk/RAIDProcessor.cpp
        disk/disk.cpp
        disk/array.cpp
        RAID/arithmetic.cpp
        RAID/RS.cpp
        RAID/RAID5.cpp
        src/misc.cpp
        src/main.cpp
        src/usecase.cpp
        src/locker.cpp
        confuse/confuse.c
        confuse/lexer.c
)
