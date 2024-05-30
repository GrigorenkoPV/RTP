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
if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    add_compile_options(-O3 -finline)
endif ()

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
        RAID/RTP.cpp
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(testbed PUBLIC "-fsanitize=undefined,address")
    target_link_options(testbed PUBLIC "-fsanitize=undefined,address")
endif ()
