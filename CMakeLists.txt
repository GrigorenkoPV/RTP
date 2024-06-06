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

# Enable assertions in std for Debug builds
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (NOT (MSVC))
        add_definitions("-D_GLIBCXX_DEBUG")
        add_definitions("-D_LIBCPP_ENABLE_ASSERTIONS=1")
    endif ()
endif ()

# Optimize non-Debug builds more
if (NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    add_compile_options(-O3 -finline)
endif ()

# LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT error)
if (NOT (lto_supported))
    message(STATUS "IPO / LTO not supported: <${error}>")
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "IPO / LTO supported, but not enabled (Debug build)")
else ()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "IPO / LTO enabled")
endif ()

# Needed for arithmetic.cpp
add_compile_options(-mavx)

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

# Enable sanitizers for Debug builds
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(testbed PUBLIC "-fsanitize=undefined,address")
    target_link_options(testbed PUBLIC "-fsanitize=undefined,address")
endif ()
